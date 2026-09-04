/**
 * @file DebugDraw.cpp
 * @brief 線分だけを積んで描くデバッグ描画の実装。
 */
#include "Pch.h"

#if FANG_ENABLE_DEBUG_DRAW

#include "Core/Log/Assert.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/DebugDraw.h"
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
		 * @brief 箱の辺 12 本を結ぶ頂点の組。
		 * @details Aabb::GetCorners と同じ並び（0〜3 が近い面、4〜7 が奥の面）を前提にする。
		 *          近い面 4 辺・奥の面 4 辺・近い⇔奥をつなぐ 4 辺で 12 本。
		 */
		constexpr uint32_t WIRE_BOX_EDGE_INDICES[12][2] = {
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, // 近い面
			{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, // 奥の面
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // 近い⇔奥
		};
	} // namespace


	bool DebugDraw::Initialize(rhi::GraphicsDevice& device)
	{
		m_device = &device;

		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float3, offsetof(DebugLineVertex, position) },
			{ "COLOR", 0, rhi::EnVertexFormat::Float4, offsetof(DebugLineVertex, color) },
		};

		// シェーダーは UnlitRenderer と共用。頂点レイアウトが一致するので新しい HLSL は要らない。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShader = rhi::MakeShaderSource(
			std::span<const uint8_t>(g_UnlitVS, sizeof(g_UnlitVS)),
			"Engine/Renderer/Shaders/UnlitVS.hlsl",
			"VertexMain"
		);
		pipelineDesc.pixelShader = rhi::MakeShaderSource(
			std::span<const uint8_t>(g_UnlitPS, sizeof(g_UnlitPS)),
			"Engine/Renderer/Shaders/UnlitPS.hlsl",
			"PixelMain"
		);
		pipelineDesc.vertexLayout = VERTEX_LAYOUT;
		pipelineDesc.topology     = rhi::EnPrimitiveTopology::LineList;

		// メッシュには隠れてほしいが、深度を汚して他の描画物を隠したくないので書き込みだけ切る。
		pipelineDesc.isDepthTestEnabled  = true;
		pipelineDesc.isDepthWriteEnabled = false;

		pipelineDesc.hasObjectConstantBuffer = true;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		m_vertexBuffer = device.CreateDynamicBuffer(
			MAX_LINE_COUNT * 2 * sizeof(DebugLineVertex),
			sizeof(DebugLineVertex),
			rhi::EnBufferKind::Vertex
		);
		if (!m_vertexBuffer.IsValid())
		{
			return false;
		}

		m_constantBuffer = device.CreateDynamicBuffer(sizeof(UnlitObjectConstants), 0, rhi::EnBufferKind::Constant);
		if (!m_constantBuffer.IsValid())
		{
			return false;
		}

		FANG_LOG_INFO(Renderer, "デバッグ線描画の準備ができた");

		return true;
	}


	void DebugDraw::Shutdown(rhi::GraphicsDevice& device)
	{
		device.DestroyBuffer(m_constantBuffer);
		device.DestroyBuffer(m_vertexBuffer);
		device.DestroyPipeline(m_pipeline);

		m_constantBuffer = {};
		m_vertexBuffer   = {};
		m_pipeline       = {};

		m_device = nullptr;
	}


	void DebugDraw::Reset()
	{
		m_lineCount = 0;
	}


	void DebugDraw::AddLine(const Vector3& from, const Vector3& to, const Vector3& color)
	{
		if (m_lineCount >= MAX_LINE_COUNT)
		{
			FANG_LOG_WARNING(Renderer, "デバッグ線を {} 本使い切った。これ以上は捨てる", MAX_LINE_COUNT);
			return;
		}

		const uint32_t vertexIndex = m_lineCount * 2;

		// アルファは使わないので常に 1.0 で埋める。
		m_vertices[vertexIndex + 0] =
			DebugLineVertex{ { from.x, from.y, from.z }, { color.x, color.y, color.z, 1.0f } };
		m_vertices[vertexIndex + 1] = DebugLineVertex{ { to.x, to.y, to.z }, { color.x, color.y, color.z, 1.0f } };

		++m_lineCount;
	}


	void DebugDraw::AddWireBox(const Aabb& bounds, const Vector3& color)
	{
		if (!bounds.IsValid())
		{
			return;
		}

		Vector3 corners[8];
		bounds.GetCorners(corners);

		AddWireBoxCorners(corners, color);
	}


	void DebugDraw::AddWireBoxCorners(std::span<const Vector3> corners, const Vector3& color)
	{
		FANG_ASSERT(corners.size() == 8, "軸に沿わない箱は 8 頂点で渡すこと");
		if (corners.size() != 8)
		{
			return;
		}

		for (const auto& edgeIndices : WIRE_BOX_EDGE_INDICES)
		{
			AddLine(corners[edgeIndices[0]], corners[edgeIndices[1]], color);
		}
	}


	void DebugDraw::AddAxes(const Matrix4x4& world, float length)
	{
		const Vector3 origin = TransformPoint(Vector3{}, world);

		AddLine(origin, TransformPoint(Vector3{ length, 0.0f, 0.0f }, world), Vector3{ 1.0f, 0.0f, 0.0f });
		AddLine(origin, TransformPoint(Vector3{ 0.0f, length, 0.0f }, world), Vector3{ 0.0f, 1.0f, 0.0f });
		AddLine(origin, TransformPoint(Vector3{ 0.0f, 0.0f, length }, world), Vector3{ 0.0f, 0.0f, 1.0f });
	}


	void DebugDraw::AddPass(
		RenderGraph&          graph,
		RenderGraphResourceId backBuffer,
		RenderGraphResourceId depthBuffer,
		const Matrix4x4&      viewProjection
	)
	{
		m_viewProjection = viewProjection;

		m_passRecordArguments = PassRecordArguments{
			.debugDraw = this,
			.device    = m_device,
		};

		RenderGraphPassDesc passDesc{};
		passDesc.name               = "DebugLinePass";
		passDesc.recordThread       = EnPassRecordThread::Job;
		passDesc.colorTarget        = backBuffer;
		passDesc.colorLoadOperation = EnLoadOperation::Load;
		passDesc.depthTarget        = depthBuffer;
		passDesc.depthLoadOperation = EnLoadOperation::Load;
		passDesc.record             = &DebugDraw::RecordDebugLinePass;
		passDesc.userData           = &m_passRecordArguments;

		graph.AddPass(passDesc);
	}


	void DebugDraw::RecordDebugLinePass(void* userData, rhi::CommandList& commandList)
	{
		const auto& arguments = *static_cast<const PassRecordArguments*>(userData);
		arguments.debugDraw->RecordLines(*arguments.device, commandList);
	}


	void DebugDraw::RecordLines(rhi::GraphicsDevice& device, rhi::CommandList& commandList)
	{
		if (m_lineCount == 0)
		{
			return;
		}

		const uint32_t vertexCount = m_lineCount * 2;

		device.UpdateBuffer(m_vertexBuffer, m_vertices, vertexCount * sizeof(DebugLineVertex));

		const UnlitObjectConstants constants{ .transform = m_viewProjection };
		device.UpdateBuffer(m_constantBuffer, &constants, sizeof(constants));

		commandList.SetPipeline(m_pipeline);
		commandList.SetObjectConstantBuffer(m_constantBuffer);
		commandList.SetVertexBuffer(m_vertexBuffer);
		commandList.Draw(vertexCount);
	}
} // namespace fang

#endif // FANG_ENABLE_DEBUG_DRAW
