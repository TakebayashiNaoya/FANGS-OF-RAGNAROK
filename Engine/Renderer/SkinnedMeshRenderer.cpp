/**
 * @file SkinnedMeshRenderer.cpp
 * @brief スキンメッシュを描くレンダラの実装。
 */
#include "Pch.h"
#include "Renderer/SkinnedMeshRenderer.h"
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
#include "SkinnedMeshVS.h"


namespace fang
{
	namespace
	{
		/**
		 * @brief シェーダに渡す頂点。
		 * @details 並びは SkinnedMeshVS.hlsl との契約なのでヘッダに出さない。
		 *          法線は 8 bit SNORM、UV は half に圧縮して持つ ➡ 入力アセンブラが float へ展開する。
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

		/**
		 * @brief 描くもの 1 個ぶんのルート定数を組む。
		 * @details 行ベクトル規約なので MVP は World が左に来る。行列は行優先のまま転置せずに渡す。
		 *          HLSL の定数バッファは既定で列優先に読むので、読む側で転置が掛かって辻褄が合う
		 *          （シェーダは mul(行列, ベクトル) と書く。骨行列も同じ規則で b1 に置いてある）。
		 *          MeshRenderer 側と同じ作り。RenderGraph でレンダラを畳むときに一緒に片付ける。
		 */
		[[nodiscard]] MeshObjectConstants MakeObjectConstants(
			const View&      view,
			const Matrix4x4& world,
			float            metallicFactor,
			float            roughnessFactor
		)
		{
			const Vector3 lightDirection = view.directionToLight;
			const Vector3 lightColor     = view.lightColor;
			const Vector3 ambientColor   = view.ambientColor;
			const Vector3 cameraPosition = view.cameraPosition;

			MeshObjectConstants constants{};
			constants.modelViewProjection = Multiply(world, view.viewProjection);
			constants.world               = world;

			constants.directionToLight = { lightDirection.x, lightDirection.y, lightDirection.z, 0.0f };
			constants.lightColor       = { lightColor.x, lightColor.y, lightColor.z, view.lightIntensity };
			constants.ambientColor     = { ambientColor.x, ambientColor.y, ambientColor.z, 0.0f };
			constants.cameraPosition   = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f };
			constants.material         = { metallicFactor, roughnessFactor, 0.0f, 0.0f };

			return constants;
		}

		/** @brief 骨行列の置き場 1 本ぶんの大きさ。 */
		constexpr uint32_t JOINT_MATRIX_BUFFER_SIZE =
			SkinnedMeshRenderer::MAX_JOINT_COUNT * static_cast<uint32_t>(sizeof(Matrix4x4));

		/**
		 * @brief ベースカラーが無いときの色。テクスチャを貼る前の単色と同じ値の sRGB 表現。
		 * @details MeshRenderer 側と同じ作り。レンダラごとに 1×1 を 1 枚持つ重複は、RenderGraph で
		 *          レンダラを畳むときに一緒に片付ける。
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


	bool SkinnedMeshRenderer::Initialize(rhi::GraphicsDevice& device)
	{
		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(SkinnedMeshVertex, position) },
			{ "NORMAL", 0, rhi::EnVertexFormat::SByte4Normalized, offsetof(SkinnedMeshVertex, normal) },
			{ "TEXCOORD", 0, rhi::EnVertexFormat::Half2, offsetof(SkinnedMeshVertex, texCoord) },
			{ "BLENDINDICES", 0, rhi::EnVertexFormat::UByte4, offsetof(SkinnedMeshVertex, joints) },
			{ "BLENDWEIGHT", 0, rhi::EnVertexFormat::Float4, offsetof(SkinnedMeshVertex, weights) },
		};

		// ピクセルシェーダーは静的メッシュと共有する。出力の並び（Mesh.hlsli）が同じで、
		// 陰影の付け方も変える理由がない ➡ 同じ絵を出すシェーダーを 2 本持たない。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_SkinnedMeshVS, sizeof(g_SkinnedMeshVS));
		pipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_MeshPS, sizeof(g_MeshPS));
		pipelineDesc.vertexLayout         = VERTEX_LAYOUT;

		// MVP・ライト・マテリアルは b0 のルート CBV（MeshConstants.h）、骨行列は b1 の定数バッファ。
		// b0 をルート定数にしないのは、実機のドライバが 16 DWORD 超のルート定数のパイプライン生成で
		// デバイスロストするため。
		// ベースカラーは t0。無いときはダミーを差すので、パイプラインは常にこの 1 本。
		pipelineDesc.hasObjectConstantBuffer = true;
		pipelineDesc.hasConstantBuffer       = true;
		pipelineDesc.hasTexture              = true;
		pipelineDesc.isDepthTestEnabled      = true;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		m_dummyBaseColor = CreateDummyBaseColor(device);
		if (!m_dummyBaseColor.IsValid())
		{
			return false;
		}

		for (rhi::BufferHandle& buffer : m_jointMatrixBuffers)
		{
			buffer = device.CreateDynamicBuffer(JOINT_MATRIX_BUFFER_SIZE, 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		for (rhi::BufferHandle& buffer : m_objectConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(sizeof(MeshObjectConstants), 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		FANG_LOG_INFO(Renderer, "スキンメッシュ描画の準備ができた");

		return true;
	}


	void SkinnedMeshRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		for (rhi::BufferHandle& buffer : m_objectConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		for (rhi::BufferHandle& buffer : m_jointMatrixBuffers)
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

		device.DestroyPipeline(m_pipeline);
		m_pipeline = {};
	}


	MeshId SkinnedMeshRenderer::CreateMesh(rhi::GraphicsDevice& device, const SkinnedMeshSource& source)
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
			const Vector3      position = source.positions[index];
			const Vector3      normal   = source.normals[index];
			const Vector2      texCoord = source.texCoords[index];
			const JointIndices joints   = source.jointIndices[index];
			const Vector4      weights  = source.jointWeights[index];

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

			vertex.position[0] = position.x;
			vertex.position[1] = position.y;
			vertex.position[2] = position.z;

			vertex.normal[0] = PackSignedNormalized8(normal.x);
			vertex.normal[1] = PackSignedNormalized8(normal.y);
			vertex.normal[2] = PackSignedNormalized8(normal.z);
			vertex.normal[3] = 0;

			vertex.texCoord[0] = PackFloat16(texCoord.x);
			vertex.texCoord[1] = PackFloat16(texCoord.y);

			vertex.joints[0] = joints.joints[0];
			vertex.joints[1] = joints.joints[1];
			vertex.joints[2] = joints.joints[2];
			vertex.joints[3] = joints.joints[3];

			vertex.weights[0] = weights.x;
			vertex.weights[1] = weights.y;
			vertex.weights[2] = weights.z;
			vertex.weights[3] = weights.w;
		}

		const rhi::BufferHandle vertexBuffer = device.CreateBuffer(
			vertices.data(),
			static_cast<uint32_t>(vertices.size() * sizeof(SkinnedMeshVertex)),
			static_cast<uint32_t>(sizeof(SkinnedMeshVertex)),
			rhi::EnBufferKind::Vertex
		);
		if (!vertexBuffer.IsValid())
		{
			return MeshId{};
		}

		// インデックスの形式は stride で決まる（2 なら R16_UINT）。
		const rhi::BufferHandle indexBuffer = device.CreateBuffer(
			source.indices.data(),
			static_cast<uint32_t>(source.indices.size() * sizeof(uint16_t)),
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
				.indexCount   = static_cast<uint32_t>(source.indices.size()),
			}
		);

		FANG_LOG_INFO(
			Renderer,
			"スキンメッシュを作った: 頂点 {} 個 / インデックス {} 個",
			vertices.size(),
			source.indices.size()
		);

		return MeshId{ .index = static_cast<uint32_t>(m_meshes.size() - 1) };
	}


	void SkinnedMeshRenderer::Draw(
		rhi::GraphicsDevice&               device,
		rhi::CommandList&                  commandList,
		const View&                        view,
		std::span<const SkinnedRenderItem> items
	)
	{
		// 初期化に失敗していても落とさない。モデルが出ないだけで、ほかの描画は続けられる。
		if (!m_pipeline.IsValid() || items.empty())
		{
			return;
		}

		commandList.SetPipeline(m_pipeline);

		uint32_t usedBufferCount = 0;
		for (const SkinnedRenderItem& item : items)
		{
			// 無効な番号は CreateMesh が失敗した合図で、想定内の入力。黙って飛ばす。
			if (!item.mesh.IsValid())
			{
				continue;
			}

			// こちらは作った覚えのない番号を渡された場合で、呼び出し側の間違い。
			if (item.mesh.index >= m_meshes.size())
			{
				FANG_ASSERT(false, "描こうとしたメッシュの番号が SkinnedMeshRenderer の持ち物でない");
				continue;
			}

			if (usedBufferCount >= MAX_ITEM_COUNT)
			{
				FANG_LOG_WARNING(
					Renderer,
					"1 フレームに描けるスキンメッシュは {} 体まで。残りを飛ばした",
					static_cast<uint32_t>(MAX_ITEM_COUNT)
				);
				break;
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

			// 単位行列で埋めてから受け取ったぶんを書く ➡ 行列が足りない・空のときはバインドポーズで出る
			// （重みの合計が 1 なので、単位行列を掛けると元の頂点に戻る）。
			Matrix4x4 jointMatrices[MAX_JOINT_COUNT];
			for (size_t index = 0; index < item.skinningMatrices.size(); ++index)
			{
				jointMatrices[index] = item.skinningMatrices[index];
			}

			device.UpdateBuffer(m_jointMatrixBuffers[usedBufferCount], jointMatrices, JOINT_MATRIX_BUFFER_SIZE);

			const Mesh& mesh = m_meshes[item.mesh.index];

			const MeshObjectConstants constants =
				MakeObjectConstants(view, item.world, item.metallicFactor, item.roughnessFactor);
			device.UpdateBuffer(m_objectConstantBuffers[usedBufferCount], &constants, sizeof(constants));

			commandList.SetVertexBuffer(mesh.vertexBuffer);
			commandList.SetIndexBuffer(mesh.indexBuffer);
			commandList.SetObjectConstantBuffer(m_objectConstantBuffers[usedBufferCount]);
			commandList.SetConstantBuffer(m_jointMatrixBuffers[usedBufferCount]);
			commandList.SetTexture(item.baseColor.IsValid() ? item.baseColor : m_dummyBaseColor);
			commandList.DrawIndexed(mesh.indexCount, 0, 0);

			++usedBufferCount;
		}
	}
} // namespace fang
