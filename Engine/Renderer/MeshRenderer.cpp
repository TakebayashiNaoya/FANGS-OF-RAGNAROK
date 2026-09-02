/**
 * @file MeshRenderer.cpp
 * @brief インデックス付きのメッシュを単色で描くレンダラの実装。
 */
#include "Pch.h"
#include "Renderer/MeshRenderer.h"
#include "Core/Log/Assert.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"
#include <cstddef>


// FXC の /Fh が吐くヘッダは BYTE 型の配列なので、<windows.h> を入れずに済むよう自前で合わせる。
using BYTE = unsigned char;
#include "MeshPS.h"
#include "MeshVS.h"


namespace fang
{
	namespace
	{
		/**
		 * @brief シェーダに渡す頂点。
		 * @details 並びは MeshVS.hlsl との契約なのでヘッダに出さない。MeshSource のばらばらの配列をここへ詰め直す。
		 */
		struct MeshVertex
		{
			float position[3];
			float normal[3];
			float texCoord[2];
		};

		/** @brief ルート定数で渡す MVP 行列の要素数。 */
		constexpr uint32_t MATRIX_ELEMENT_COUNT = 16;

		/** @brief 16 bit のインデックスで指せる頂点の数。 */
		constexpr size_t MAX_VERTEX_COUNT = 65536;

		constexpr Vector3 DEFAULT_NORMAL    = { 0.0f, 1.0f, 0.0f };
		constexpr Vector2 DEFAULT_TEX_COORD = { 0.0f, 0.0f };
	} // namespace


	bool MeshRenderer::Initialize(rhi::GraphicsDevice& device)
	{
		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(MeshVertex, position) },
			{ "NORMAL", 0, rhi::EnVertexFormat::Float3, offsetof(MeshVertex, normal) },
			{ "TEXCOORD", 0, rhi::EnVertexFormat::Float2, offsetof(MeshVertex, texCoord) },
		};

		// シェーダーは Shaders/*.hlsl をビルド時に FXC でヘッダ化したもの。UWP に実行時コンパイルが無いため。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_MeshVS, sizeof(g_MeshVS));
		pipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_MeshPS, sizeof(g_MeshPS));
		pipelineDesc.vertexLayout         = VERTEX_LAYOUT;

		// MVP は b0 のルート定数で渡す。1 メッシュ 1 マテリアルのうちは定数バッファを回すより少ない手数で済む。
		pipelineDesc.rootConstantCount = MATRIX_ELEMENT_COUNT;

		// 立体は前後関係が要るので深度テストを有効にする。
		pipelineDesc.isDepthTestEnabled = true;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		FANG_LOG_INFO(Renderer, "メッシュ描画の準備ができた");

		return true;
	}


	void MeshRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		for (const Mesh& mesh : m_meshes)
		{
			device.DestroyBuffer(mesh.indexBuffer);
			device.DestroyBuffer(mesh.vertexBuffer);
		}
		m_meshes.clear();

		device.DestroyPipeline(m_pipeline);
		m_pipeline = {};
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
			const Vector3 position = source.positions[index];
			const Vector3 normal   = hasNormals ? source.normals[index] : DEFAULT_NORMAL;
			const Vector2 texCoord = hasTexCoords ? source.texCoords[index] : DEFAULT_TEX_COORD;

			MeshVertex& vertex = vertices[index];

			vertex.position[0] = position.x;
			vertex.position[1] = position.y;
			vertex.position[2] = position.z;

			vertex.normal[0] = normal.x;
			vertex.normal[1] = normal.y;
			vertex.normal[2] = normal.z;

			vertex.texCoord[0] = texCoord.x;
			vertex.texCoord[1] = texCoord.y;
		}

		const rhi::BufferHandle vertexBuffer = device.CreateBuffer(
			vertices.data(),
			static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex)),
			static_cast<uint32_t>(sizeof(MeshVertex)),
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
			"メッシュを作った: 頂点 {} 個 / インデックス {} 個",
			vertices.size(),
			source.indices.size()
		);

		return MeshId{ .index = static_cast<uint32_t>(m_meshes.size() - 1) };
	}


	void MeshRenderer::Draw(rhi::CommandList& commandList, const View& view, std::span<const RenderItem> items) const
	{
		// 初期化に失敗していても落とさない。モデルが出ないだけで、ほかの描画は続けられる。
		if (!m_pipeline.IsValid() || items.empty())
		{
			return;
		}

		commandList.SetPipeline(m_pipeline);

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

			// 行ベクトル規約なので World が左に来る。
			const Matrix4x4 modelViewProjection = Multiply(item.world, view.viewProjection);

			// 行優先のまま、転置せずにルート定数へ渡す。HLSL の定数バッファは既定で列優先に読むので、
			// 読む側で転置が掛かって辻褄が合う（MeshVS.hlsl は mul(mvp, position) と書く）。
			// 片側だけ流儀を変えると、絵が崩れているのに数字は正しく見える厄介な歪みになる。
			commandList.SetVertexBuffer(mesh.vertexBuffer);
			commandList.SetIndexBuffer(mesh.indexBuffer);
			commandList.SetRootConstants(&modelViewProjection, MATRIX_ELEMENT_COUNT);
			commandList.DrawIndexed(mesh.indexCount, 0, 0);
		}
	}
} // namespace fang
