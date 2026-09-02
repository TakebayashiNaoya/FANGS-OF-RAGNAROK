/**
 * @file TriangleRenderer.cpp
 * @brief 三角形 1 枚を描く暫定レンダラの実装。
 */
#include "Pch.h"
#include "Renderer/TriangleRenderer.h"
#include "Core/Log/Assert.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"


FANG_DEFINE_LOG_CATEGORY(Renderer);


// FXC の /Fh が吐くヘッダは BYTE 型の配列なので、<windows.h> を入れずに済むよう自前で合わせる。
using BYTE = unsigned char;
#include "TrianglePS.h"
#include "TriangleVS.h"


namespace fang
{
	namespace
	{
		struct TriangleVertex
		{
			float position[3];
			float color[4];
		};

		constexpr TriangleVertex TRIANGLE_VERTICES[] = {
			{ { 0.0f, 0.6f, 0.0f }, { 0.90f, 0.32f, 0.24f, 1.0f } },
			{ { 0.5f, -0.4f, 0.0f }, { 0.32f, 0.70f, 0.45f, 1.0f } },
			{ { -0.5f, -0.4f, 0.0f }, { 0.30f, 0.55f, 0.90f, 1.0f } },
		};

	} // namespace


	bool TriangleRenderer::Initialize(rhi::GraphicsDevice& device)
	{
		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(TriangleVertex, position) },
			{ "COLOR", 0, rhi::EnVertexFormat::Float4, offsetof(TriangleVertex, color) },
		};

		// シェーダーは Shaders/*.hlsl をビルド時に FXC でヘッダ化したもの。UWP に実行時コンパイルが無いため。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_TriangleVS, sizeof(g_TriangleVS));
		pipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_TrianglePS, sizeof(g_TrianglePS));
		pipelineDesc.vertexLayout         = VERTEX_LAYOUT;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		m_vertexBuffer = device.CreateBuffer(
			TRIANGLE_VERTICES,
			sizeof(TRIANGLE_VERTICES),
			sizeof(TriangleVertex),
			rhi::EnBufferKind::Vertex
		);
		if (!m_vertexBuffer.IsValid())
		{
			return false;
		}

		FANG_LOG_INFO(Renderer, "三角形の準備ができた");

		return true;
	}


	void TriangleRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		device.DestroyBuffer(m_vertexBuffer);
		device.DestroyPipeline(m_pipeline);
		m_vertexBuffer = {};
		m_pipeline     = {};
	}


	void TriangleRenderer::Draw(rhi::CommandList& commandList, uint32_t width, uint32_t height) const
	{
		FANG_ASSERT(m_pipeline.IsValid(), "TriangleRenderer が初期化されていない");

		commandList.SetViewport(width, height);
		commandList.SetPipeline(m_pipeline);
		commandList.SetVertexBuffer(m_vertexBuffer);
		commandList.Draw(static_cast<uint32_t>(FANG_COUNT_OF(TRIANGLE_VERTICES)));
	}
} // namespace fang
