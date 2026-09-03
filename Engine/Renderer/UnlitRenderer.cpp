/**
 * @file UnlitRenderer.cpp
 * @brief 頂点色をそのまま出す非ライティングのレンダラの実装。
 */
#include "Pch.h"
#include "Renderer/UnlitRenderer.h"
#include "Core/Log/Assert.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"
#include "Renderer/Shaders/UnlitConstants.h"
#include <cstddef>


// FXC の /Fh が吐くヘッダは BYTE 型の配列なので、<windows.h> を入れずに済むよう自前で合わせる。
using BYTE = unsigned char;
#include "UnlitPS.h"
#include "UnlitVS.h"


namespace fang
{
	namespace
	{
		/**
		 * @brief シェーダに渡す頂点。
		 * @details 並びは Unlit.hlsli の VertexInput との契約なのでヘッダに出さない。
		 *          メッシュと違って圧縮しないのは、DebugDraw が毎フレーム作り直す線分を
		 *          そのまま積めるほうが大事で、量も出ないため。
		 */
		struct UnlitVertex
		{
			float position[3];
			float color[4];
		};

		/**
		 * @brief 組み込みの三角形。座標はクリップ空間そのままで持つ。
		 * @details RHI の疎通を目で見るためのもの。DebugDraw が外から頂点を受け取れるようになったら消す。
		 */
		constexpr UnlitVertex TRIANGLE_VERTICES[] = {
			{ { 0.0f, 0.6f, 0.0f }, { 0.90f, 0.32f, 0.24f, 1.0f } },
			{ { 0.5f, -0.4f, 0.0f }, { 0.32f, 0.70f, 0.45f, 1.0f } },
			{ { -0.5f, -0.4f, 0.0f }, { 0.30f, 0.55f, 0.90f, 1.0f } },
		};
	} // namespace


	bool UnlitRenderer::Initialize(rhi::GraphicsDevice& device)
	{
		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(UnlitVertex, position) },
			{ "COLOR", 0, rhi::EnVertexFormat::Float4, offsetof(UnlitVertex, color) },
		};

		// シェーダーは Shaders/*.hlsl をビルド時に FXC でヘッダ化したもの。UWP に実行時コンパイルが無いため。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_UnlitVS, sizeof(g_UnlitVS));
		pipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_UnlitPS, sizeof(g_UnlitPS));
		pipelineDesc.vertexLayout         = VERTEX_LAYOUT;

		// 行列は b0 のルート CBV で渡す（UnlitConstants.h）。深度テストは持たない。
		pipelineDesc.hasObjectConstantBuffer = true;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		m_vertexBuffer = device.CreateBuffer(
			TRIANGLE_VERTICES,
			sizeof(TRIANGLE_VERTICES),
			sizeof(UnlitVertex),
			rhi::EnBufferKind::Vertex
		);
		if (!m_vertexBuffer.IsValid())
		{
			return false;
		}

		m_objectConstantBuffer =
			device.CreateDynamicBuffer(sizeof(UnlitObjectConstants), 0, rhi::EnBufferKind::Constant);
		if (!m_objectConstantBuffer.IsValid())
		{
			return false;
		}

		FANG_LOG_INFO(Renderer, "頂点色描画の準備ができた");

		return true;
	}


	void UnlitRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		device.DestroyBuffer(m_objectConstantBuffer);
		device.DestroyBuffer(m_vertexBuffer);
		device.DestroyPipeline(m_pipeline);

		m_objectConstantBuffer = {};
		m_vertexBuffer         = {};
		m_pipeline             = {};
	}


	void UnlitRenderer::DrawTriangle(
		rhi::GraphicsDevice& device,
		rhi::CommandList&    commandList,
		const Matrix4x4&     transform
	)
	{
		FANG_ASSERT(m_pipeline.IsValid(), "UnlitRenderer が初期化されていない");

		const UnlitObjectConstants constants{ .transform = transform };
		device.UpdateBuffer(m_objectConstantBuffer, &constants, sizeof(constants));

		commandList.SetPipeline(m_pipeline);
		commandList.SetObjectConstantBuffer(m_objectConstantBuffer);
		commandList.SetVertexBuffer(m_vertexBuffer);
		commandList.Draw(static_cast<uint32_t>(FANG_COUNT_OF(TRIANGLE_VERTICES)));
	}
} // namespace fang
