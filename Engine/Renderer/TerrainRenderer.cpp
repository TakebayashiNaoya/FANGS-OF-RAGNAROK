/**
 * @file TerrainRenderer.cpp
 * @brief ハイトマップ地形を描くレンダラの実装。
 */
#include "Pch.h"
#include "Renderer/TerrainRenderer.h"
#include "Core/Log/Assert.h"
#include "Core/Math/Pack.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"
#include "Renderer/Shaders/TerrainConstants.h"
#include <cstddef>
#include <vector>


// FXC の /Fh が吐くヘッダは BYTE 型の配列なので、<windows.h> を入れずに済むよう自前で合わせる。
using BYTE = unsigned char;
#include "TerrainPS.h"
#include "TerrainVS.h"


namespace fang
{
	namespace
	{
		/**
		 * @brief シェーダに渡す地形の頂点。
		 * @details 並びは TerrainVS.hlsl との契約なのでヘッダに出さない。位置はワールド座標のまま、
		 *          法線は 8 bit SNORM に圧縮する（MeshRenderer と同じ流儀）。UV は持たない
		 *          ➡ シェーダがワールド座標から作るので、頂点 16 バイトで足りる。
		 */
		struct TerrainVertex
		{
			float  position[3]; /**< ワールド座標。地形の寸法精度は落とさない。 */
			int8_t normal[4];   /**< SNORM。w は未使用で 0。 */
		};

		static_assert(sizeof(TerrainVertex) == 16, "頂点の大きさが契約の 16 バイトからずれている");

		/** @brief 16 bit のインデックスで指せる頂点の数。 */
		constexpr size_t MAX_VERTEX_COUNT = 65536;
	} // namespace


	bool TerrainRenderer::Initialize(rhi::GraphicsDevice& device)
	{
		m_device = &device;

		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(TerrainVertex, position) },
			{ "NORMAL", 0, rhi::EnVertexFormat::SByte4Normalized, offsetof(TerrainVertex, normal) },
		};

		// t0 = スプラット、t1〜t3 = レイヤのアルベド、その次の t4 = シャドウマップ。
		// サンプラはレイヤのタイリングのため WRAP（スプラット側はシェーダが UV をクランプして守る）。
		// b0 は地形の定数（ロード時に 1 回書くだけ）、b1 はシーン View の MeshFrameConstants を借りる。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_TerrainVS, sizeof(g_TerrainVS));
		pipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_TerrainPS, sizeof(g_TerrainPS));
		pipelineDesc.vertexLayout         = VERTEX_LAYOUT;

		pipelineDesc.hasObjectConstantBuffer = true;
		pipelineDesc.hasFrameConstantBuffer  = true;
		pipelineDesc.textureCount            = 4;
		pipelineDesc.samplerAddressMode      = rhi::EnSamplerAddressMode::Wrap;
		pipelineDesc.hasShadowMap            = true;
		pipelineDesc.isDepthTestEnabled      = true;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		m_constantBuffer = device.CreateDynamicBuffer(sizeof(TerrainConstants), 0, rhi::EnBufferKind::Constant);
		if (!m_constantBuffer.IsValid())
		{
			return false;
		}

		FANG_LOG_INFO(Renderer, "地形描画の準備ができた");

		return true;
	}


	void TerrainRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		DestroyChunks(device);

		device.DestroyBuffer(m_constantBuffer);
		device.DestroyPipeline(m_pipeline);

		m_constantBuffer = {};
		m_pipeline       = {};

		m_surface = {};
		m_device  = nullptr;
	}


	bool TerrainRenderer::CreateTerrain(
		rhi::GraphicsDevice&          device,
		std::span<const TerrainChunk> chunks,
		const TerrainSurface&         surface
	)
	{
		// 作り直しに備えて前の地形を捨てる。失敗したときも「地形なし」の状態で返せる。
		DestroyChunks(device);

		if (!m_pipeline.IsValid())
		{
			FANG_LOG_ERROR(Renderer, "地形描画が初期化されていない");
			return false;
		}

		if (chunks.empty() || chunks.size() > MAX_CHUNK_COUNT)
		{
			FANG_LOG_ERROR(
				Renderer,
				"地形チャンクの数が不正だ: {} 個。持てるのは 1〜{} 個",
				chunks.size(),
				static_cast<uint32_t>(MAX_CHUNK_COUNT)
			);
			return false;
		}

		if (!surface.splatmap.IsValid() || !surface.layerAlbedos[0].IsValid() || !surface.layerAlbedos[1].IsValid() ||
			!surface.layerAlbedos[2].IsValid())
		{
			FANG_LOG_ERROR(Renderer, "地形のテクスチャがそろっていない");
			return false;
		}

		if (surface.halfWidth <= 0.0f || surface.halfDepth <= 0.0f || surface.layerTileCentimeters <= 0.0f ||
			surface.splatTexelCount == 0)
		{
			FANG_LOG_ERROR(Renderer, "地形の寸法の設定が不正だ");
			return false;
		}

		// 詰め直しの作業領域。読み込みのときにしか通らないので、ここでのヒープ確保は許す。
		std::vector<TerrainVertex> vertices;

		for (const TerrainChunk& chunk : chunks)
		{
			if (chunk.positions.empty() || chunk.indices.empty() || chunk.positions.size() != chunk.normals.size() ||
				chunk.positions.size() > MAX_VERTEX_COUNT)
			{
				FANG_LOG_ERROR(Renderer, "地形チャンクの中身が不正だ");
				DestroyChunks(device);
				return false;
			}

			vertices.clear();
			vertices.resize(chunk.positions.size());
			for (size_t index = 0; index < vertices.size(); ++index)
			{
				const Vector3& position = chunk.positions[index];
				const Vector3& normal   = chunk.normals[index];

				TerrainVertex& vertex = vertices[index];
				vertex.position[0]    = position.x;
				vertex.position[1]    = position.y;
				vertex.position[2]    = position.z;

				vertex.normal[0] = PackSignedNormalized8(normal.x);
				vertex.normal[1] = PackSignedNormalized8(normal.y);
				vertex.normal[2] = PackSignedNormalized8(normal.z);
				vertex.normal[3] = 0;
			}

			const rhi::BufferHandle vertexBuffer = device.CreateBuffer(
				vertices.data(),
				static_cast<uint32_t>(vertices.size() * sizeof(TerrainVertex)),
				static_cast<uint32_t>(sizeof(TerrainVertex)),
				rhi::EnBufferKind::Vertex
			);
			if (!vertexBuffer.IsValid())
			{
				DestroyChunks(device);
				return false;
			}

			const rhi::BufferHandle indexBuffer = device.CreateBuffer(
				chunk.indices.data(),
				static_cast<uint32_t>(chunk.indices.size() * sizeof(uint16_t)),
				static_cast<uint32_t>(sizeof(uint16_t)),
				rhi::EnBufferKind::Index
			);
			if (!indexBuffer.IsValid())
			{
				device.DestroyBuffer(vertexBuffer);
				DestroyChunks(device);
				return false;
			}

			m_chunks[m_chunkCount] = Chunk{
				.vertexBuffer = vertexBuffer,
				.indexBuffer  = indexBuffer,
				.indexCount   = static_cast<uint32_t>(chunk.indices.size()),
				.bounds       = chunk.bounds,
			};
			++m_chunkCount;
		}

		m_surface = surface;

		// 地形は動かないので b0 はここで 1 回書くだけ。フレームごとのプールが要らない。
		const TerrainConstants constants{
			.sizeParameters = { surface.halfWidth,
								surface.halfDepth,
								1.0f / surface.layerTileCentimeters,
								0.5f / static_cast<float>(surface.splatTexelCount) },
			.layerRoughness = { surface.layerRoughness[0], surface.layerRoughness[1], surface.layerRoughness[2], 0.0f },
		};
		device.UpdateBuffer(m_constantBuffer, &constants, sizeof(constants));

		FANG_LOG_INFO(Renderer, "地形を作った: チャンク {} 個", m_chunkCount);

		return true;
	}


	void TerrainRenderer::AddPass(
		RenderGraph&          graph,
		RenderGraphResourceId backBuffer,
		RenderGraphResourceId depthBuffer,
		RenderGraphResourceId shadowMapResource,
		rhi::BufferHandle     frameConstantBuffer,
		rhi::TextureHandle    shadowMap,
		const Matrix4x4&      viewProjection
	)
	{
		if (m_chunkCount == 0)
		{
			return;
		}

		m_frustum.ExtractFromViewProjection(viewProjection);

		m_frameConstantBuffer = frameConstantBuffer;
		m_shadowMap           = shadowMap;

		m_passRecordArguments = PassRecordArguments{
			.renderer = this,
			.device   = m_device,
		};

		RenderGraphPassDesc passDesc{};
		passDesc.name               = "TerrainPass";
		passDesc.recordThread       = EnPassRecordThread::Job;
		passDesc.colorTarget        = backBuffer;
		passDesc.colorLoadOperation = EnLoadOperation::Load;
		passDesc.depthTarget        = depthBuffer;
		passDesc.depthLoadOperation = EnLoadOperation::Load;
		passDesc.record             = &TerrainRenderer::RecordTerrainPass;
		passDesc.userData           = &m_passRecordArguments;

		// シャドウマップを読むリソースとして宣言する ➡ ShadowPass が書いた後に読むためのバリアは
		// Compile が前後関係から導く。
		passDesc.readResources[0]  = shadowMapResource;
		passDesc.readResourceCount = 1;

		graph.AddPass(passDesc);
	}


	void TerrainRenderer::RecordTerrainPass(void* userData, rhi::CommandList& commandList)
	{
		const auto& arguments = *static_cast<const PassRecordArguments*>(userData);
		arguments.renderer->RecordChunks(commandList);
	}


	void TerrainRenderer::RecordChunks(rhi::CommandList& commandList)
	{
		m_drawnChunkCount = 0;

		if (m_chunkCount == 0 || !m_frameConstantBuffer.IsValid())
		{
			return;
		}

		// TODO: 実機で「手前の地形が見えない」の切り分けが済んだら消す。
		// 初回の記録だけ、チャンクごとのカリング判定と箱を startup.log に残す
		// ➡ CPU 側で落ちているのか、描いているのに映らないのかを実機のログで見分ける。
		if (!m_hasLoggedCullingDiagnostics)
		{
			m_hasLoggedCullingDiagnostics = true;
			for (uint32_t index = 0; index < m_chunkCount; ++index)
			{
				const Aabb& bounds = m_chunks[index].bounds;
				FANG_LOG_INFO(
					Renderer,
					"地形チャンク {:2}: 判定 {} / X {:5.0f}〜{:5.0f} / Y {:3.0f}〜{:3.0f} / Z {:5.0f}〜{:5.0f}",
					index,
					m_frustum.Intersects(bounds) ? "内" : "外",
					bounds.min.x,
					bounds.max.x,
					bounds.min.y,
					bounds.max.y,
					bounds.min.z,
					bounds.max.z
				);
			}
		}

		// パイプラインと定数とテクスチャは全チャンク共通なので一度だけ差す。
		commandList.SetPipeline(m_pipeline);
		commandList.SetObjectConstantBuffer(m_constantBuffer);
		commandList.SetFrameConstantBuffer(m_frameConstantBuffer);
		commandList.SetTexture(0, m_surface.splatmap);
		commandList.SetTexture(1, m_surface.layerAlbedos[0]);
		commandList.SetTexture(2, m_surface.layerAlbedos[1]);
		commandList.SetTexture(3, m_surface.layerAlbedos[2]);
		commandList.SetShadowMap(m_shadowMap);

		for (uint32_t index = 0; index < m_chunkCount; ++index)
		{
			const Chunk& chunk = m_chunks[index];

			if (!m_frustum.Intersects(chunk.bounds))
			{
				continue;
			}

			commandList.SetVertexBuffer(chunk.vertexBuffer);
			commandList.SetIndexBuffer(chunk.indexBuffer);
			commandList.DrawIndexed(chunk.indexCount, 0, 0);

			++m_drawnChunkCount;
		}
	}


	void TerrainRenderer::DestroyChunks(rhi::GraphicsDevice& device)
	{
		for (uint32_t index = 0; index < m_chunkCount; ++index)
		{
			device.DestroyBuffer(m_chunks[index].indexBuffer);
			device.DestroyBuffer(m_chunks[index].vertexBuffer);
			m_chunks[index] = {};
		}

		m_chunkCount      = 0;
		m_drawnChunkCount = 0;
	}
} // namespace fang
