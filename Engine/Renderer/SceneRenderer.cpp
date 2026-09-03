/**
 * @file SceneRenderer.cpp
 * @brief View・カリング・フレーム定数をまとめ、ScenePass を記録する実装。
 */
#include "Pch.h"
#include "Renderer/SceneRenderer.h"
#include "Core/Log/Assert.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"
#include "Renderer/Shaders/MeshConstants.h"


namespace fang
{
	namespace
	{
		/** @brief 視錐台カリングを生き延びた分を積む作業領域の上限。 */
		constexpr uint32_t MAX_CULLED_ITEM_COUNT = 16;

		/** @brief フレームの間ずっと同じ定数を組む。行列は行優先のまま転置しない（MeshRenderer と同じ流儀）。 */
		[[nodiscard]] MeshFrameConstants MakeFrameConstants(const View& view)
		{
			const Vector3 cameraPosition = view.cameraPosition;
			const Vector3 lightDirection = view.directionToLight;
			const Vector3 lightColor     = view.lightColor;
			const Vector3 ambientColor   = view.ambientColor;

			MeshFrameConstants constants{};
			constants.viewProjection = view.viewProjection;

			constants.cameraPosition   = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f };
			constants.directionToLight = { lightDirection.x, lightDirection.y, lightDirection.z, 0.0f };
			constants.lightColor       = { lightColor.x, lightColor.y, lightColor.z, view.lightIntensity };
			constants.ambientColor     = { ambientColor.x, ambientColor.y, ambientColor.z, 0.0f };

			return constants;
		}
	} // namespace


	bool SceneRenderer::Initialize(rhi::GraphicsDevice& device, MeshRenderer& meshRenderer)
	{
		m_device       = &device;
		m_meshRenderer = &meshRenderer;

		for (rhi::BufferHandle& buffer : m_frameConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(sizeof(MeshFrameConstants), 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		FANG_LOG_INFO(Renderer, "シーン描画の準備ができた");

		return true;
	}


	void SceneRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		for (rhi::BufferHandle& buffer : m_frameConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		m_meshRenderer = nullptr;
		m_device       = nullptr;
	}


	void SceneRenderer::Reset()
	{
		m_viewCount = 0;

		for (uint32_t viewIndex = 0; viewIndex < MAX_VIEW_COUNT; ++viewIndex)
		{
			m_submittedItems[viewIndex]  = {};
			m_drawnItemCounts[viewIndex] = 0;
		}
	}


	ViewId SceneRenderer::AddView(rhi::GraphicsDevice& device, const View& view)
	{
		FANG_ASSERT(m_viewCount < MAX_VIEW_COUNT, "1 フレームに追加できる View を使い切った");

		if (m_viewCount >= MAX_VIEW_COUNT)
		{
			return ViewId{};
		}

		const uint32_t viewIndex = m_viewCount;
		++m_viewCount;

		m_frustums[viewIndex].ExtractFromViewProjection(view.viewProjection);

		// 視点と光は描画物が変わっても変わらないので、この View に対しては 1 フレームに 1 回だけ書く。
		const MeshFrameConstants frameConstants = MakeFrameConstants(view);
		device.UpdateBuffer(m_frameConstantBuffers[viewIndex], &frameConstants, sizeof(frameConstants));

		return ViewId{ .index = viewIndex };
	}


	void SceneRenderer::Submit(ViewId view, std::span<const RenderItem> items)
	{
		if (!view.IsValid() || view.index >= m_viewCount)
		{
			FANG_ASSERT(false, "登録していない View へ Submit しようとしている");
			return;
		}

		m_submittedItems[view.index] = items;
	}


	void SceneRenderer::AddPasses(
		RenderGraph&           graph,
		RenderGraphResourceId  backBuffer,
		RenderGraphResourceId  depthBuffer,
		const rhi::ClearColor& clearColor,
		EnLoadOperation        loadOperation
	)
	{
		for (uint32_t viewIndex = 0; viewIndex < m_viewCount; ++viewIndex)
		{
			m_passRecordArguments[viewIndex] = ScenePassRecordArguments{
				.sceneRenderer = this,
				.viewIndex     = viewIndex,
				.device        = m_device,
			};

			// 最初の View だけ呼び出し側の指定（Clear か Load か）に従い、以降は前の View が描いたものの上に重ねる。
			const EnLoadOperation viewLoadOperation = (viewIndex == 0) ? loadOperation : EnLoadOperation::Load;

			RenderGraphPassDesc passDesc{};
			passDesc.name               = "ScenePass";
			passDesc.recordThread       = EnPassRecordThread::Job;
			passDesc.colorTarget        = backBuffer;
			passDesc.colorLoadOperation = viewLoadOperation;
			passDesc.clearColor         = clearColor;
			passDesc.depthTarget        = depthBuffer;
			passDesc.depthLoadOperation = viewLoadOperation;
			passDesc.record             = &SceneRenderer::RecordScenePass;
			passDesc.userData           = &m_passRecordArguments[viewIndex];

			graph.AddPass(passDesc);
		}
	}


	uint32_t SceneRenderer::GetLastDrawnItemCount() const
	{
		uint32_t total = 0;
		for (uint32_t viewIndex = 0; viewIndex < m_viewCount; ++viewIndex)
		{
			total += m_drawnItemCounts[viewIndex];
		}

		return total;
	}


	void SceneRenderer::RecordScenePass(void* userData, rhi::CommandList& commandList)
	{
		const auto& arguments = *static_cast<const ScenePassRecordArguments*>(userData);
		arguments.sceneRenderer->RecordView(arguments.viewIndex, *arguments.device, commandList);
	}


	void SceneRenderer::RecordView(uint32_t viewIndex, rhi::GraphicsDevice& device, rhi::CommandList& commandList)
	{
		const Frustum&                    frustum = m_frustums[viewIndex];
		const std::span<const RenderItem> items   = m_submittedItems[viewIndex];

		// ヒープ確保が禁じられたジョブの中なので、生き残った分はスタック上の固定長配列へ積む。
		RenderItem visibleItems[MAX_CULLED_ITEM_COUNT];
		uint32_t   visibleItemCount = 0;

		for (const RenderItem& item : items)
		{
			if (item.bounds.IsValid() && !frustum.Intersects(item.bounds))
			{
				continue;
			}

			if (visibleItemCount >= MAX_CULLED_ITEM_COUNT)
			{
				FANG_LOG_WARNING(
					Renderer,
					"カリングを生き延びても描けるのは {} 個まで。残りを飛ばした",
					MAX_CULLED_ITEM_COUNT
				);
				break;
			}

			visibleItems[visibleItemCount] = item;
			++visibleItemCount;
		}

		m_drawnItemCounts[viewIndex] = visibleItemCount;

		m_meshRenderer->Draw(
			device,
			commandList,
			m_frameConstantBuffers[viewIndex],
			std::span<const RenderItem>(visibleItems, visibleItemCount)
		);
	}
} // namespace fang
