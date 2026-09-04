/**
 * @file MeshRenderer.cpp
 * @brief 静的メッシュとスキンメッシュを描くレンダラの実装。
 */
#include "Pch.h"
#include "Renderer/MeshRenderer.h"
#include "Core/Log/Assert.h"
#include "Core/Math/Pack.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"
#include "Renderer/Shaders/MeshConstants.h"
#include <cstddef>


// FXC の /Fh が吐くヘッダは BYTE 型の配列なので、<windows.h> を入れずに済むよう自前で合わせる。
using BYTE = unsigned char;
#include "MeshPS.h"
#include "MeshVS.h"
#include "SkinnedMeshVS.h"


namespace fang
{
	namespace
	{
		/**
		 * @brief シェーダに渡す静的メッシュの頂点。
		 * @details 並びは MeshVS.hlsl との契約なのでヘッダに出さない。MeshSource のばらばらの配列をここへ詰め直す。
		 *          法線は 8 bit SNORM、UV は half に圧縮して持つ ➡ 入力アセンブラが float へ展開するので、
		 *          シェーダは float3 / float2 のまま変わらない。
		 */
		struct MeshVertex
		{
			float    position[3]; /**< モデルの寸法精度は落とさない。 */
			int8_t   normal[4];   /**< SNORM。w は未使用で 0。接線を足す日は別属性で足す。 */
			uint16_t texCoord[2]; /**< half のビット列。 */
		};

		static_assert(sizeof(MeshVertex) == 20, "頂点の大きさが契約の 20 バイトからずれている");

		/**
		 * @brief シェーダに渡すスキンメッシュの頂点。
		 * @details 並びは SkinnedMeshVS.hlsl との契約。前半は MeshVertex と同じ形にしてあるので、
		 *          圧縮の手順を PackCommonVertex に寄せられる。
		 *          関節の番号は 8 bit × 4 で足りる（狼は 59 関節）。重みは合計 1 に正規化済みのものを
		 *          そのまま持つ ➡ 量子化して合計がずれると、変形の乱れの原因が 1 つ増える。
		 */
		struct SkinnedMeshVertex
		{
			float    position[3]; /**< モデルの寸法精度は落とさない。 */
			int8_t   normal[4];   /**< SNORM。w は未使用で 0。 */
			uint16_t texCoord[2]; /**< half のビット列。 */
			uint8_t  joints[4];
			float    weights[4];
		};

		static_assert(sizeof(SkinnedMeshVertex) == 40, "頂点の大きさが契約の 40 バイトからずれている");

		/** @brief 16 bit のインデックスで指せる頂点の数。 */
		constexpr size_t MAX_VERTEX_COUNT = 65536;

		/** @brief 骨行列の置き場 1 本ぶんの大きさ。 */
		constexpr uint32_t SKINNING_CONSTANT_BUFFER_SIZE =
			MeshRenderer::MAX_JOINT_COUNT * static_cast<uint32_t>(sizeof(Matrix4x4));

		/** @brief 深度パスの一律バイアス。縞（自己遮蔽の誤判定）を消す。PSO に焼き込まれるため実行時調整の口は無い。 */
		constexpr int32_t SHADOW_DEPTH_BIAS = 500;

		/** @brief 深度パスの傾き比例バイアス。光に対して斜めな面ほど 1 テクセルの中の深度差が大きいのを補う。 */
		constexpr float SHADOW_SLOPE_SCALED_DEPTH_BIAS = 1.5f;

		constexpr Vector3 DEFAULT_NORMAL    = { 0.0f, 1.0f, 0.0f };
		constexpr Vector2 DEFAULT_TEX_COORD = { 0.0f, 0.0f };

		/**
		 * @brief 位置・法線・UV を頂点へ圧縮して書き込む。
		 * @details 静的とスキンで頂点の前半が同じ並びなので、2 か所に同じ圧縮を書かずに済むようテンプレートにした。
		 */
		template <typename TVertex>
		void PackCommonVertex(
			const Vector3& position,
			const Vector3& normal,
			const Vector2& texCoord,
			TVertex*       outVertex
		)
		{
			outVertex->position[0] = position.x;
			outVertex->position[1] = position.y;
			outVertex->position[2] = position.z;

			outVertex->normal[0] = PackSignedNormalized8(normal.x);
			outVertex->normal[1] = PackSignedNormalized8(normal.y);
			outVertex->normal[2] = PackSignedNormalized8(normal.z);
			outVertex->normal[3] = 0;

			outVertex->texCoord[0] = PackFloat16(texCoord.x);
			outVertex->texCoord[1] = PackFloat16(texCoord.y);
		}

		/**
		 * @brief 描くもの 1 個ぶんの定数を組む。
		 * @details 行列は行優先のまま転置せずに渡す。HLSL の定数バッファは既定で列優先に読むので、
		 *          読む側で転置が掛かって辻褄が合う（シェーダは mul(行列, ベクトル) と書く）。
		 *          片側だけ流儀を変えると、絵が崩れているのに数字は正しく見える厄介な歪みになる。
		 */
		[[nodiscard]] MeshObjectConstants MakeObjectConstants(
			const Matrix4x4& world,
			float            metallicFactor,
			float            roughnessFactor
		)
		{
			MeshObjectConstants constants{};
			constants.world    = world;
			constants.material = { metallicFactor, roughnessFactor, 0.0f, 0.0f };

			return constants;
		}

		/**
		 * @brief ベースカラーが無いときの色。テクスチャを貼る前の単色と同じ値の sRGB 表現。
		 * @details 読み込みに失敗したときの見た目を従来と変えないための値。シェーダは常にサンプルするので、
		 *          「テクスチャがあるか」の分岐がどこにも要らなくなる。
		 */
		constexpr uint8_t DUMMY_BASE_COLOR[4] = { 199, 194, 184, 255 };

		/** @brief 1×1 のダミーテクスチャを作る。失敗したら無効なハンドル。 */
		[[nodiscard]] rhi::TextureHandle CreateDummyBaseColor(rhi::GraphicsDevice& device)
		{
			const rhi::TextureMipLevel mipLevel{
				.pixels      = DUMMY_BASE_COLOR,
				.width       = 1,
				.height      = 1,
				.rowPitch    = 4,
				.sizeInBytes = 4,
			};

			const rhi::TextureSource source{
				.mipLevels = std::span<const rhi::TextureMipLevel>(&mipLevel, 1),
				.format    = rhi::EnTextureFormat::RGBA8Srgb,
			};

			return device.CreateTexture2D(source);
		}
	} // namespace


	bool MeshRenderer::Initialize(rhi::GraphicsDevice& device)
	{
		constexpr rhi::VertexAttribute STATIC_VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(MeshVertex, position) },
			{ "NORMAL", 0, rhi::EnVertexFormat::SByte4Normalized, offsetof(MeshVertex, normal) },
			{ "TEXCOORD", 0, rhi::EnVertexFormat::Half2, offsetof(MeshVertex, texCoord) },
		};

		constexpr rhi::VertexAttribute SKINNED_VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(SkinnedMeshVertex, position) },
			{ "NORMAL", 0, rhi::EnVertexFormat::SByte4Normalized, offsetof(SkinnedMeshVertex, normal) },
			{ "TEXCOORD", 0, rhi::EnVertexFormat::Half2, offsetof(SkinnedMeshVertex, texCoord) },
			{ "BLENDINDICES", 0, rhi::EnVertexFormat::UByte4, offsetof(SkinnedMeshVertex, joints) },
			{ "BLENDWEIGHT", 0, rhi::EnVertexFormat::Float4, offsetof(SkinnedMeshVertex, weights) },
		};

		// シェーダーは Shaders/*.hlsl をビルド時に FXC でヘッダ化したもの。UWP に実行時コンパイルが無いため。
		// ピクセルシェーダーは静的とスキンで共有する。出力の並び（Mesh.hlsli）が同じで、陰影の付け方も
		// 変える理由がない ➡ 同じ絵を出すシェーダーを 2 本持たない。
		//
		// world と材質は b0、視点と光は b1、骨行列は b2 のルート CBV で渡す（MeshConstants.h）。
		// ルート定数にしないのは、実機のドライバが 16 DWORD 超のルート定数のパイプライン生成で
		// デバイスロストするため。ベースカラーは t0 で、無いときはダミーを差すのでパイプラインは分岐しない。
		// 立体は前後関係が要るので深度テストを有効にする。
		rhi::GraphicsPipelineDesc staticPipelineDesc{};
		staticPipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_MeshVS, sizeof(g_MeshVS));
		staticPipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_MeshPS, sizeof(g_MeshPS));
		staticPipelineDesc.vertexLayout         = STATIC_VERTEX_LAYOUT;

		staticPipelineDesc.hasObjectConstantBuffer = true;
		staticPipelineDesc.hasFrameConstantBuffer  = true;
		staticPipelineDesc.textureCount            = 1;
		staticPipelineDesc.hasShadowMap            = true;
		staticPipelineDesc.isDepthTestEnabled      = true;

		m_staticPipeline = device.CreateGraphicsPipeline(staticPipelineDesc);
		if (!m_staticPipeline.IsValid())
		{
			return false;
		}

		rhi::GraphicsPipelineDesc skinnedPipelineDesc = staticPipelineDesc;
		skinnedPipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_SkinnedMeshVS, sizeof(g_SkinnedMeshVS));
		skinnedPipelineDesc.vertexLayout         = SKINNED_VERTEX_LAYOUT;

		skinnedPipelineDesc.hasSkinningConstantBuffer = true;

		m_skinnedPipeline = device.CreateGraphicsPipeline(skinnedPipelineDesc);
		if (!m_skinnedPipeline.IsValid())
		{
			return false;
		}

		// シャドウマップに深度だけを焼くパス。頂点シェーダーは色パスと同じバイトコードを再利用する
		// （b1 に光の viewProjection を差すだけで同じ変換が使える）➡ 新しい頂点シェーダーも頂点契約の
		// 複製も要らない。PS を空にすると描画先 0 本の深度専用 PSO になる（GraphicsPipelineDesc の規約）。
		rhi::GraphicsPipelineDesc staticDepthPipelineDesc{};
		staticDepthPipelineDesc.vertexShaderBytecode    = std::span<const uint8_t>(g_MeshVS, sizeof(g_MeshVS));
		staticDepthPipelineDesc.vertexLayout            = STATIC_VERTEX_LAYOUT;
		staticDepthPipelineDesc.hasObjectConstantBuffer = true;
		staticDepthPipelineDesc.hasFrameConstantBuffer  = true;
		staticDepthPipelineDesc.isDepthTestEnabled      = true;
		staticDepthPipelineDesc.depthBias               = SHADOW_DEPTH_BIAS;
		staticDepthPipelineDesc.slopeScaledDepthBias    = SHADOW_SLOPE_SCALED_DEPTH_BIAS;

		m_staticDepthPipeline = device.CreateGraphicsPipeline(staticDepthPipelineDesc);
		if (!m_staticDepthPipeline.IsValid())
		{
			return false;
		}

		rhi::GraphicsPipelineDesc skinnedDepthPipelineDesc = staticDepthPipelineDesc;
		skinnedDepthPipelineDesc.vertexShaderBytecode =
			std::span<const uint8_t>(g_SkinnedMeshVS, sizeof(g_SkinnedMeshVS));
		skinnedDepthPipelineDesc.vertexLayout              = SKINNED_VERTEX_LAYOUT;
		skinnedDepthPipelineDesc.hasSkinningConstantBuffer = true;

		m_skinnedDepthPipeline = device.CreateGraphicsPipeline(skinnedDepthPipelineDesc);
		if (!m_skinnedDepthPipeline.IsValid())
		{
			return false;
		}

		m_dummyBaseColor = CreateDummyBaseColor(device);
		if (!m_dummyBaseColor.IsValid())
		{
			return false;
		}

		for (rhi::BufferHandle& buffer : m_objectConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(sizeof(MeshObjectConstants), 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		for (rhi::BufferHandle& buffer : m_skinningConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(SKINNING_CONSTANT_BUFFER_SIZE, 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		for (rhi::BufferHandle& buffer : m_depthObjectConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(sizeof(MeshObjectConstants), 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		for (rhi::BufferHandle& buffer : m_depthSkinningConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(SKINNING_CONSTANT_BUFFER_SIZE, 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		FANG_LOG_INFO(Renderer, "メッシュ描画の準備ができた");

		return true;
	}


	void MeshRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		for (rhi::BufferHandle& buffer : m_depthSkinningConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		for (rhi::BufferHandle& buffer : m_depthObjectConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		for (rhi::BufferHandle& buffer : m_skinningConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		for (rhi::BufferHandle& buffer : m_objectConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		for (const Mesh& mesh : m_meshes)
		{
			device.DestroyBuffer(mesh.indexBuffer);
			device.DestroyBuffer(mesh.vertexBuffer);
		}
		m_meshes.clear();

		device.DestroyTexture(m_dummyBaseColor);
		m_dummyBaseColor = {};

		device.DestroyPipeline(m_skinnedDepthPipeline);
		m_skinnedDepthPipeline = {};

		device.DestroyPipeline(m_staticDepthPipeline);
		m_staticDepthPipeline = {};

		device.DestroyPipeline(m_skinnedPipeline);
		m_skinnedPipeline = {};

		device.DestroyPipeline(m_staticPipeline);
		m_staticPipeline = {};
	}


	MeshId MeshRenderer::CreateMesh(rhi::GraphicsDevice& device, const MeshSource& source)
	{
		if (source.positions.empty() || source.indices.empty())
		{
			FANG_LOG_ERROR(Renderer, "メッシュの位置かインデックスが空だ");
			return MeshId{};
		}

		if (source.positions.size() > MAX_VERTEX_COUNT)
		{
			FANG_LOG_ERROR(
				Renderer,
				"頂点が多すぎる: {} 個。16 bit インデックスで指せるのは {} 個まで",
				source.positions.size(),
				MAX_VERTEX_COUNT
			);
			return MeshId{};
		}

		const bool hasNormals   = !source.normals.empty();
		const bool hasTexCoords = !source.texCoords.empty();
		if (hasNormals && source.normals.size() != source.positions.size())
		{
			FANG_LOG_ERROR(
				Renderer,
				"法線の数が位置と合っていない: {} と {}",
				source.normals.size(),
				source.positions.size()
			);
			return MeshId{};
		}

		if (hasTexCoords && source.texCoords.size() != source.positions.size())
		{
			FANG_LOG_ERROR(
				Renderer,
				"UV の数が位置と合っていない: {} と {}",
				source.texCoords.size(),
				source.positions.size()
			);
			return MeshId{};
		}

		// 詰め直しの作業領域。読み込みのときにしか通らないので、ここでのヒープ確保は許す。
		std::vector<MeshVertex> vertices(source.positions.size());
		for (size_t index = 0; index < vertices.size(); ++index)
		{
			PackCommonVertex(
				source.positions[index],
				hasNormals ? source.normals[index] : DEFAULT_NORMAL,
				hasTexCoords ? source.texCoords[index] : DEFAULT_TEX_COORD,
				&vertices[index]
			);
		}

		return RegisterMesh(
			device,
			vertices.data(),
			static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex)),
			static_cast<uint32_t>(sizeof(MeshVertex)),
			source.indices,
			MakeAabbFromPoints(source.positions),
			false
		);
	}


	MeshId MeshRenderer::CreateMesh(rhi::GraphicsDevice& device, const SkinnedMeshSource& source)
	{
		const size_t vertexCount = source.positions.size();
		if (vertexCount == 0 || source.indices.empty())
		{
			FANG_LOG_ERROR(Renderer, "スキンメッシュの位置かインデックスが空だ");
			return MeshId{};
		}

		if (vertexCount > MAX_VERTEX_COUNT)
		{
			FANG_LOG_ERROR(
				Renderer,
				"頂点が多すぎる: {} 個。16 bit インデックスで指せるのは {} 個まで",
				vertexCount,
				MAX_VERTEX_COUNT
			);
			return MeshId{};
		}

		if (source.normals.size() != vertexCount || source.texCoords.size() != vertexCount ||
			source.jointIndices.size() != vertexCount || source.jointWeights.size() != vertexCount)
		{
			FANG_LOG_ERROR(Renderer, "スキンメッシュの属性ごとに頂点数が違う");
			return MeshId{};
		}

		// 詰め直しの作業領域。読み込みのときにしか通らないので、ここでのヒープ確保は許す。
		std::vector<SkinnedMeshVertex> vertices(vertexCount);
		for (size_t index = 0; index < vertexCount; ++index)
		{
			const JointIndices joints  = source.jointIndices[index];
			const Vector4      weights = source.jointWeights[index];

			if (joints.joints[0] >= MAX_JOINT_COUNT || joints.joints[1] >= MAX_JOINT_COUNT ||
				joints.joints[2] >= MAX_JOINT_COUNT || joints.joints[3] >= MAX_JOINT_COUNT)
			{
				FANG_LOG_ERROR(
					Renderer,
					"関節の番号がシェーダの上限（{}）を超えている",
					static_cast<uint32_t>(MAX_JOINT_COUNT)
				);
				return MeshId{};
			}

			SkinnedMeshVertex& vertex = vertices[index];
			PackCommonVertex(source.positions[index], source.normals[index], source.texCoords[index], &vertex);

			vertex.joints[0] = joints.joints[0];
			vertex.joints[1] = joints.joints[1];
			vertex.joints[2] = joints.joints[2];
			vertex.joints[3] = joints.joints[3];

			vertex.weights[0] = weights.x;
			vertex.weights[1] = weights.y;
			vertex.weights[2] = weights.z;
			vertex.weights[3] = weights.w;
		}

		return RegisterMesh(
			device,
			vertices.data(),
			static_cast<uint32_t>(vertices.size() * sizeof(SkinnedMeshVertex)),
			static_cast<uint32_t>(sizeof(SkinnedMeshVertex)),
			source.indices,
			MakeAabbFromPoints(source.positions),
			true
		);
	}


	MeshId MeshRenderer::RegisterMesh(
		rhi::GraphicsDevice&      device,
		const void*               vertices,
		uint32_t                  sizeInBytes,
		uint32_t                  strideInBytes,
		std::span<const uint16_t> indices,
		const Aabb&               localBounds,
		bool                      isSkinned
	)
	{
		const rhi::BufferHandle vertexBuffer =
			device.CreateBuffer(vertices, sizeInBytes, strideInBytes, rhi::EnBufferKind::Vertex);
		if (!vertexBuffer.IsValid())
		{
			return MeshId{};
		}

		// インデックスの形式は stride で決まる（2 なら R16_UINT）。
		const rhi::BufferHandle indexBuffer = device.CreateBuffer(
			indices.data(),
			static_cast<uint32_t>(indices.size() * sizeof(uint16_t)),
			static_cast<uint32_t>(sizeof(uint16_t)),
			rhi::EnBufferKind::Index
		);
		if (!indexBuffer.IsValid())
		{
			device.DestroyBuffer(vertexBuffer);
			return MeshId{};
		}

		m_meshes.push_back(
			Mesh{
				.vertexBuffer = vertexBuffer,
				.indexBuffer  = indexBuffer,
				.indexCount   = static_cast<uint32_t>(indices.size()),
				.localBounds  = localBounds,
				.isSkinned    = isSkinned,
			}
		);

		FANG_LOG_INFO(
			Renderer,
			"{}メッシュを作った: 頂点 {} 個 / インデックス {} 個",
			isSkinned ? "スキン" : "静的",
			sizeInBytes / strideInBytes,
			indices.size()
		);

		return MeshId{ .index = static_cast<uint32_t>(m_meshes.size() - 1) };
	}


	Aabb MeshRenderer::GetLocalBounds(MeshId mesh) const
	{
		if (!mesh.IsValid() || mesh.index >= m_meshes.size())
		{
			return Aabb{};
		}

		return m_meshes[mesh.index].localBounds;
	}


	void MeshRenderer::Draw(
		rhi::GraphicsDevice&        device,
		rhi::CommandList&           commandList,
		rhi::BufferHandle           frameConstantBuffer,
		rhi::TextureHandle          shadowMap,
		std::span<const RenderItem> items
	)
	{
		// 初期化に失敗していても落とさない。モデルが出ないだけで、ほかの描画は続けられる。
		if (items.empty() || !frameConstantBuffer.IsValid())
		{
			return;
		}

		// シャドウマップは起動時に確保し切る前提の必須リソースで、無効なハンドルは呼び出し側の配線漏れ。
		// castsShadow のように「無くても描ける」ものではないのでアサートで止める。
		FANG_ASSERT(shadowMap.IsValid(), "シャドウマップが未生成のまま Draw が呼ばれた");

		// D3D12 はルートシグネチャを差し替えると根に差したものが外れるので、静的とスキンを行き来したら
		// b1 も差し直す。前に差したのがどちらだったかをこの 2 つで覚えておく。
		bool hasBoundPipeline       = false;
		bool isBoundPipelineSkinned = false;

		// b0(オブジェクト定数)は静的・スキン共通、b2(骨行列)はスキンだけが使う ➡ 本数が違うので
		// カウンタも別に持つ。共通 1 本のままだと b2 も MAX_ITEM_COUNT ぶん確保する羽目になる。
		uint32_t usedObjectBufferCount   = 0;
		uint32_t usedSkinningBufferCount = 0;
		for (const RenderItem& item : items)
		{
			// 無効な番号は CreateMesh が失敗した合図で、想定内の入力。黙って飛ばす。
			if (!item.mesh.IsValid())
			{
				continue;
			}

			// こちらは作った覚えのない番号を渡された場合で、呼び出し側の間違い。
			if (item.mesh.index >= m_meshes.size())
			{
				FANG_ASSERT(false, "描こうとしたメッシュの番号が MeshRenderer の持ち物でない");
				continue;
			}

			const Mesh& mesh = m_meshes[item.mesh.index];

			const rhi::PipelineHandle pipeline = mesh.isSkinned ? m_skinnedPipeline : m_staticPipeline;
			if (!pipeline.IsValid())
			{
				continue;
			}

			// b0 は全アイテム共通の置き場。ここが尽きたら静的もスキンも描けないので打ち切る。
			if (usedObjectBufferCount >= MAX_ITEM_COUNT)
			{
				FANG_LOG_WARNING(
					Renderer,
					"1 フレームに描けるメッシュは {} 個まで。残りを飛ばした",
					static_cast<uint32_t>(MAX_ITEM_COUNT)
				);
				break;
			}

			uint32_t skinningBufferIndex = 0;
			if (mesh.isSkinned)
			{
				// b2 はスキンだけの置き場で本数が少ない。ここに掛かってもこのアイテムだけ飛ばし、
				// あとに続く静的メッシュの描画は止めない。
				if (usedSkinningBufferCount >= MAX_SKINNED_ITEM_COUNT)
				{
					FANG_LOG_WARNING(
						Renderer,
						"1 フレームに描けるスキンメッシュは {} 個まで。このアイテムを飛ばした",
						static_cast<uint32_t>(MAX_SKINNED_ITEM_COUNT)
					);
					continue;
				}

				if (item.skinningMatrices.size() > MAX_JOINT_COUNT)
				{
					FANG_LOG_ERROR(
						Renderer,
						"スキニング行列が多すぎる: {} 本。シェーダが持つのは {} 本",
						item.skinningMatrices.size(),
						static_cast<uint32_t>(MAX_JOINT_COUNT)
					);
					continue;
				}

				skinningBufferIndex = usedSkinningBufferCount;

				// 単位行列で埋めてから受け取ったぶんを書く ➡ 行列が足りない・空のときはバインドポーズで出る
				// （重みの合計が 1 なので、単位行列を掛けると元の頂点に戻る）。
				Matrix4x4 jointMatrices[MAX_JOINT_COUNT];
				for (size_t index = 0; index < item.skinningMatrices.size(); ++index)
				{
					jointMatrices[index] = item.skinningMatrices[index];
				}

				device.UpdateBuffer(
					m_skinningConstantBuffers[skinningBufferIndex],
					jointMatrices,
					SKINNING_CONSTANT_BUFFER_SIZE
				);
				++usedSkinningBufferCount;
			}

			const MeshObjectConstants objectConstants =
				MakeObjectConstants(item.world, item.metallicFactor, item.roughnessFactor);
			device.UpdateBuffer(
				m_objectConstantBuffers[usedObjectBufferCount],
				&objectConstants,
				sizeof(objectConstants)
			);

			// ① パイプラインを切り替えるときだけ、SetPipeline と一緒に b1(フレーム定数)も差し直す。
			// 　 最初の 1 個、または静的⇔スキンの境目でだけ通る。差し替えていないパイプラインへ毎回
			// 　 差し直す無駄をしない。
			if (!hasBoundPipeline || isBoundPipelineSkinned != mesh.isSkinned)
			{
				commandList.SetPipeline(pipeline);
				commandList.SetFrameConstantBuffer(frameConstantBuffer);
				commandList.SetShadowMap(shadowMap);

				hasBoundPipeline       = true;
				isBoundPipelineSkinned = mesh.isSkinned;
			}

			// ② 頂点・インデックス・b0(オブジェクト定数)・(スキンなら b2)を差し、Draw する。
			commandList.SetVertexBuffer(mesh.vertexBuffer);
			commandList.SetIndexBuffer(mesh.indexBuffer);
			commandList.SetObjectConstantBuffer(m_objectConstantBuffers[usedObjectBufferCount]);
			if (mesh.isSkinned)
			{
				commandList.SetSkinningConstantBuffer(m_skinningConstantBuffers[skinningBufferIndex]);
			}
			commandList.SetTexture(item.baseColor.IsValid() ? item.baseColor : m_dummyBaseColor);
			commandList.DrawIndexed(mesh.indexCount, 0, 0);

			++usedObjectBufferCount;
		}
	}


	void MeshRenderer::DrawDepth(
		rhi::GraphicsDevice&        device,
		rhi::CommandList&           commandList,
		rhi::BufferHandle           frameConstantBuffer,
		std::span<const RenderItem> items
	)
	{
		// 初期化に失敗していても落とさない。モデルが出ないだけで、ほかの描画は続けられる。
		if (items.empty() || !frameConstantBuffer.IsValid())
		{
			return;
		}

		// D3D12 はルートシグネチャを差し替えると根に差したものが外れるので、静的とスキンを行き来したら
		// b1 も差し直す。前に差したのがどちらだったかをこの 2 つで覚えておく。
		bool hasBoundPipeline       = false;
		bool isBoundPipelineSkinned = false;

		// b0(オブジェクト定数)は静的・スキン共通、b2(骨行列)はスキンだけが使う ➡ 本数が違うので
		// カウンタも別に持つ。共通 1 本のままだと b2 も MAX_ITEM_COUNT ぶん確保する羽目になる。
		uint32_t usedObjectBufferCount   = 0;
		uint32_t usedSkinningBufferCount = 0;
		for (const RenderItem& item : items)
		{
			// 無効な番号は CreateMesh が失敗した合図で、想定内の入力。黙って飛ばす。
			if (!item.mesh.IsValid())
			{
				continue;
			}

			// こちらは作った覚えのない番号を渡された場合で、呼び出し側の間違い。
			if (item.mesh.index >= m_meshes.size())
			{
				FANG_ASSERT(false, "描こうとしたメッシュの番号が MeshRenderer の持ち物でない");
				continue;
			}

			const Mesh& mesh = m_meshes[item.mesh.index];

			const rhi::PipelineHandle pipeline = mesh.isSkinned ? m_skinnedDepthPipeline : m_staticDepthPipeline;
			if (!pipeline.IsValid())
			{
				continue;
			}

			// b0 は全アイテム共通の置き場。ここが尽きたら静的もスキンも描けないので打ち切る。
			if (usedObjectBufferCount >= MAX_ITEM_COUNT)
			{
				FANG_LOG_WARNING(
					Renderer,
					"1 フレームに描けるメッシュは {} 個まで。残りを飛ばした",
					static_cast<uint32_t>(MAX_ITEM_COUNT)
				);
				break;
			}

			uint32_t skinningBufferIndex = 0;
			if (mesh.isSkinned)
			{
				// b2 はスキンだけの置き場で本数が少ない。ここに掛かってもこのアイテムだけ飛ばし、
				// あとに続く静的メッシュの描画は止めない。
				if (usedSkinningBufferCount >= MAX_SKINNED_ITEM_COUNT)
				{
					FANG_LOG_WARNING(
						Renderer,
						"1 フレームに描けるスキンメッシュは {} 個まで。このアイテムを飛ばした",
						static_cast<uint32_t>(MAX_SKINNED_ITEM_COUNT)
					);
					continue;
				}

				if (item.skinningMatrices.size() > MAX_JOINT_COUNT)
				{
					FANG_LOG_ERROR(
						Renderer,
						"スキニング行列が多すぎる: {} 本。シェーダが持つのは {} 本",
						item.skinningMatrices.size(),
						static_cast<uint32_t>(MAX_JOINT_COUNT)
					);
					continue;
				}

				skinningBufferIndex = usedSkinningBufferCount;

				// 単位行列で埋めてから受け取ったぶんを書く ➡ 行列が足りない・空のときはバインドポーズで出る
				// （重みの合計が 1 なので、単位行列を掛けると元の頂点に戻る）。
				Matrix4x4 jointMatrices[MAX_JOINT_COUNT];
				for (size_t index = 0; index < item.skinningMatrices.size(); ++index)
				{
					jointMatrices[index] = item.skinningMatrices[index];
				}

				device.UpdateBuffer(
					m_depthSkinningConstantBuffers[skinningBufferIndex],
					jointMatrices,
					SKINNING_CONSTANT_BUFFER_SIZE
				);
				++usedSkinningBufferCount;
			}

			const MeshObjectConstants objectConstants =
				MakeObjectConstants(item.world, item.metallicFactor, item.roughnessFactor);
			device.UpdateBuffer(
				m_depthObjectConstantBuffers[usedObjectBufferCount],
				&objectConstants,
				sizeof(objectConstants)
			);

			// ① パイプラインを切り替えるときだけ、SetPipeline と一緒に b1(フレーム定数)も差し直す。
			// 　 最初の 1 個、または静的⇔スキンの境目でだけ通る。差し替えていないパイプラインへ毎回
			// 　 差し直す無駄をしない。
			if (!hasBoundPipeline || isBoundPipelineSkinned != mesh.isSkinned)
			{
				commandList.SetPipeline(pipeline);
				commandList.SetFrameConstantBuffer(frameConstantBuffer);

				hasBoundPipeline       = true;
				isBoundPipelineSkinned = mesh.isSkinned;
			}

			// ② 頂点・インデックス・b0(オブジェクト定数)・(スキンなら b2)を差し、Draw する。
			// 　 深度専用パイプラインはテクスチャもシャドウマップも持たないので、そのぶんの Set は無い。
			commandList.SetVertexBuffer(mesh.vertexBuffer);
			commandList.SetIndexBuffer(mesh.indexBuffer);
			commandList.SetObjectConstantBuffer(m_depthObjectConstantBuffers[usedObjectBufferCount]);
			if (mesh.isSkinned)
			{
				commandList.SetSkinningConstantBuffer(m_depthSkinningConstantBuffers[skinningBufferIndex]);
			}
			commandList.DrawIndexed(mesh.indexCount, 0, 0);

			++usedObjectBufferCount;
		}
	}
} // namespace fang
