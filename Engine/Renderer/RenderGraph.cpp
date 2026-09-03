/**
 * @file RenderGraph.cpp
 * @brief パス宣言からバリアとクリアを導く Compile と、パスごとに記録する Execute の実装。
 */
#include "Pch.h"
#include "Renderer/RenderGraph.h"
#include "Core/Job/JobCounter.h"
#include "Core/Job/JobSystem.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"


namespace fang
{
	static_assert(
		RenderGraph::MAX_RESOURCE_COUNT <= CompiledRenderPass::MAX_BARRIER_COUNT,
		"1 パスでリソース全部の用途が変わると先頭バリア列があふれる"
	);

	static_assert(
		RenderGraph::MAX_PASS_COUNT <= rhi::GraphicsDevice::MAX_COMMAND_LIST_COUNT,
		"パスの数だけコマンドリストを借りられない"
	);


	void RenderGraph::Reset()
	{
		m_passCount        = 0;
		m_resourceCount    = 0;
		m_commandListCount = 0;

		m_backBufferResourceId = RenderGraphResourceId{};
		m_depthResourceId      = RenderGraphResourceId{};
	}


	RenderGraphResourceId RenderGraph::ImportBackBuffer()
	{
		FANG_ASSERT(!m_backBufferResourceId.IsValid(), "バックバッファを 1 フレームに二度登録しようとしている");

		if (!m_backBufferResourceId.IsValid())
		{
			m_backBufferResourceId = AddResource(rhi::EnResourceState::Present, rhi::EnResourceState::Present);
		}

		return m_backBufferResourceId;
	}


	RenderGraphResourceId RenderGraph::ImportDepthBuffer()
	{
		FANG_ASSERT(!m_depthResourceId.IsValid(), "深度バッファを 1 フレームに二度登録しようとしている");

		if (!m_depthResourceId.IsValid())
		{
			m_depthResourceId = AddResource(rhi::EnResourceState::DepthWrite, rhi::EnResourceState::DepthWrite);
		}

		return m_depthResourceId;
	}


	void RenderGraph::AddPass(const RenderGraphPassDesc& desc)
	{
		FANG_ASSERT(m_passCount < MAX_PASS_COUNT, "1 フレームに宣言できるパスを使い切った");

		if (m_passCount >= MAX_PASS_COUNT)
		{
			return;
		}

		m_passes[m_passCount] = desc;
		++m_passCount;
	}


	void RenderGraph::Compile()
	{
		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			m_compiledPasses[passIndex] = CompiledRenderPass{};
		}

		// 二度呼んでも同じ結果になるよう、追っている用途をフレームの頭へ戻してから始める。
		for (uint32_t resourceIndex = 0; resourceIndex < m_resourceCount; ++resourceIndex)
		{
			Resource& resource = m_resources[resourceIndex];

			resource.currentState     = resource.initialState;
			resource.lastUsePassIndex = INVALID_PASS_INDEX;
		}

		// 最終状態へ戻す遷移をどのパスの末尾に置くかは、全部のパスを見終えないと決まらない。
		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			const RenderGraphPassDesc& pass = m_passes[passIndex];

			MarkUse(passIndex, pass.colorTarget);
			MarkUse(passIndex, pass.depthTarget);

			for (uint32_t readIndex = 0; readIndex < pass.readResourceCount; ++readIndex)
			{
				MarkUse(passIndex, pass.readResources[readIndex]);
			}
		}

		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			const RenderGraphPassDesc& pass     = m_passes[passIndex];
			CompiledRenderPass&        compiled = m_compiledPasses[passIndex];

			RequireState(passIndex, pass.colorTarget, rhi::EnResourceState::RenderTarget);
			RequireState(passIndex, pass.depthTarget, rhi::EnResourceState::DepthWrite);

			for (uint32_t readIndex = 0; readIndex < pass.readResourceCount; ++readIndex)
			{
				RequireState(passIndex, pass.readResources[readIndex], rhi::EnResourceState::PixelShaderResource);
			}

			// クリアはバリアの後。描画先の用途に移る前に塗ると叱られる。
			if (pass.colorTarget.IsValid() && pass.colorLoadOperation == EnLoadOperation::Clear)
			{
				compiled.isColorCleared = true;
				compiled.clearColor     = pass.clearColor;
			}

			if (pass.depthTarget.IsValid() && pass.depthLoadOperation == EnLoadOperation::Clear)
			{
				compiled.isDepthCleared = true;
			}

			AddFinalBarriers(passIndex);
		}
	}


	void RenderGraph::Execute(rhi::GraphicsDevice& device, JobSystem& jobSystem)
	{
		static_assert(
			sizeof(RecordJobArguments) <= JobSystem::MAX_ARGUMENT_SIZE,
			"記録ジョブの入力がジョブ 1 件に載らない"
		);

		m_commandListCount = 0;

		if (m_passCount == 0)
		{
			return;
		}

		// 貸し出しはメインスレッド専有なので、記録を始める前に人数ぶんまとめて取る。
		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			rhi::CommandList* commandList = device.AcquireCommandList();
			if (commandList == nullptr)
			{
				// 1 本でも欠けるとパスの並びに穴が開く。そのフレームは 1 つも記録せずに畳む。
				return;
			}

			m_commandLists[passIndex] = commandList;
		}

		m_commandListCount = m_passCount;

		m_backBufferWidth  = device.GetBackBufferWidth();
		m_backBufferHeight = device.GetBackBufferHeight();

		JobCounter recordCounter;

		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			if (m_passes[passIndex].recordThread != EnPassRecordThread::Job)
			{
				continue;
			}

			const RecordJobArguments jobArguments{ .graph = this, .passIndex = passIndex };

			const JobDesc jobDesc{
				.function     = &RenderGraph::RecordPassJob,
				.arguments    = &jobArguments,
				.argumentSize = sizeof(jobArguments),
			};

			jobSystem.Submit(jobDesc, &recordCounter);
		}

		// メイン指定のパスは、ワーカーが走っている間にこちらで積む。
		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			if (m_passes[passIndex].recordThread == EnPassRecordThread::Main)
			{
				RecordPass(passIndex);
			}
		}

		jobSystem.Wait(recordCounter);
	}


	const CompiledRenderPass& RenderGraph::GetCompiledPass(uint32_t passIndex) const
	{
		FANG_ASSERT(passIndex < m_passCount, "宣言していないパスの記録手順を読もうとしている");

		// アサートが消える構成でも配列の外を読まないよう、範囲外は先頭に畳む。
		return m_compiledPasses[passIndex < m_passCount ? passIndex : 0];
	}


	std::span<rhi::CommandList* const> RenderGraph::GetCommandLists() const
	{
		return std::span<rhi::CommandList* const>(m_commandLists, m_commandListCount);
	}


	void RenderGraph::RecordPassJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto& jobArguments = *static_cast<const RecordJobArguments*>(arguments);
		jobArguments.graph->RecordPass(jobArguments.passIndex);
	}


	RenderGraphResourceId RenderGraph::AddResource(rhi::EnResourceState initialState, rhi::EnResourceState finalState)
	{
		FANG_ASSERT(m_resourceCount < MAX_RESOURCE_COUNT, "1 フレームに登録できるリソースを使い切った");

		if (m_resourceCount >= MAX_RESOURCE_COUNT)
		{
			return RenderGraphResourceId{};
		}

		Resource& resource = m_resources[m_resourceCount];

		resource.initialState     = initialState;
		resource.finalState       = finalState;
		resource.currentState     = initialState;
		resource.lastUsePassIndex = INVALID_PASS_INDEX;

		const RenderGraphResourceId id{ .index = m_resourceCount };
		++m_resourceCount;

		return id;
	}


	void RenderGraph::MarkUse(uint32_t passIndex, RenderGraphResourceId resource)
	{
		if (!resource.IsValid())
		{
			return;
		}

		FANG_ASSERT(resource.index < m_resourceCount, "登録していないリソースを使おうとしている");

		if (resource.index >= m_resourceCount)
		{
			return;
		}

		m_resources[resource.index].lastUsePassIndex = passIndex;
	}


	void RenderGraph::RequireState(uint32_t passIndex, RenderGraphResourceId resource, rhi::EnResourceState state)
	{
		if (!resource.IsValid() || resource.index >= m_resourceCount)
		{
			return;
		}

		Resource& tracked = m_resources[resource.index];
		if (tracked.currentState == state)
		{
			return;
		}

		CompiledRenderPass& compiled = m_compiledPasses[passIndex];
		FANG_ASSERT(compiled.beginBarrierCount < CompiledRenderPass::MAX_BARRIER_COUNT, "先頭バリアが多すぎる");

		if (compiled.beginBarrierCount >= CompiledRenderPass::MAX_BARRIER_COUNT)
		{
			return;
		}

		compiled.beginBarriers[compiled.beginBarrierCount] = RenderGraphBarrier{
			.resource = resource,
			.before   = tracked.currentState,
			.after    = state,
		};
		++compiled.beginBarrierCount;

		tracked.currentState = state;
	}


	void RenderGraph::AddFinalBarriers(uint32_t passIndex)
	{
		CompiledRenderPass& compiled = m_compiledPasses[passIndex];

		for (uint32_t resourceIndex = 0; resourceIndex < m_resourceCount; ++resourceIndex)
		{
			Resource& resource = m_resources[resourceIndex];
			if (resource.lastUsePassIndex != passIndex || resource.currentState == resource.finalState)
			{
				continue;
			}

			FANG_ASSERT(compiled.endBarrierCount < CompiledRenderPass::MAX_BARRIER_COUNT, "末尾バリアが多すぎる");

			if (compiled.endBarrierCount >= CompiledRenderPass::MAX_BARRIER_COUNT)
			{
				return;
			}

			compiled.endBarriers[compiled.endBarrierCount] = RenderGraphBarrier{
				.resource = RenderGraphResourceId{ .index = resourceIndex },
				.before   = resource.currentState,
				.after    = resource.finalState,
			};
			++compiled.endBarrierCount;

			resource.currentState = resource.finalState;
		}
	}


	void RenderGraph::RecordPass(uint32_t passIndex)
	{
		const RenderGraphPassDesc& pass     = m_passes[passIndex];
		const CompiledRenderPass&  compiled = m_compiledPasses[passIndex];

		rhi::CommandList& commandList = *m_commandLists[passIndex];

		for (uint32_t barrierIndex = 0; barrierIndex < compiled.beginBarrierCount; ++barrierIndex)
		{
			ApplyBarrier(commandList, compiled.beginBarriers[barrierIndex]);
		}

		// D3D12 のコマンドリストは本をまたいで状態を引き継がないので、描画先もビューポートもパスごとに差し直す。
		if (pass.colorTarget.IsValid() || pass.depthTarget.IsValid())
		{
			commandList.SetRenderTargetToBackBuffer(pass.depthTarget.IsValid());
			commandList.SetViewport(m_backBufferWidth, m_backBufferHeight);
		}

		if (compiled.isColorCleared)
		{
			commandList.ClearRenderTarget(compiled.clearColor);
		}

		if (compiled.isDepthCleared)
		{
			commandList.ClearDepth();
		}

		if (pass.record != nullptr)
		{
			pass.record(pass.userData, commandList);
		}

		for (uint32_t barrierIndex = 0; barrierIndex < compiled.endBarrierCount; ++barrierIndex)
		{
			ApplyBarrier(commandList, compiled.endBarriers[barrierIndex]);
		}
	}


	void RenderGraph::ApplyBarrier(rhi::CommandList& commandList, const RenderGraphBarrier& barrier) const
	{
		// 遷移の口があるのはバックバッファだけ。深度とオフスクリーンの口はそれを描く段で足す。
		if (barrier.resource.index != m_backBufferResourceId.index)
		{
			return;
		}

		commandList.TransitionBackBuffer(barrier.before, barrier.after);
	}
} // namespace fang
