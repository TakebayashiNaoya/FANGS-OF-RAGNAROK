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


	RenderGraphResourceId RenderGraph::ImportDepthTexture(rhi::TextureHandle texture, uint32_t width, uint32_t height)
	{
		FANG_ASSERT(texture.IsValid(), "無効なテクスチャを深度リソースとして登録しようとしている");

		return AddResource(rhi::EnResourceState::DepthWrite, rhi::EnResourceState::DepthWrite, texture, width, height);
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
		// ① 各リソースの最終使用パスを計算する。
		// 　 二度目の呼び出しでも同じ結果になるよう、まず追跡中の状態を初期状態へ戻してから、宣言順に
		// 　 MarkUse で「そのリソースを最後に使うパス」を記録し直す。
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

		// ② 宣言順の走査でバリアとクリアの指示を導く。
		// 　 パスの並び順に RequireState で用途の変化を先頭バリアへ積み、Load 操作が Clear のものは
		// 　 クリアフラグを立て、AddFinalBarriers でこのパスが最後の使用先だったリソースを最終状態へ戻す
		// 　 末尾バリアを積む。
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

		// ① コマンドリストを一括確保する。
		// 　 貸し出しはメインスレッド専有なので、ワーカーへ記録を投げ始める前にパスの数ぶんまとめて借りる。
		// 　 1 本でも借りられなければ何も記録せずに戻る。
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

		// ② ジョブへ渡す入力を準備する。
		// 　 各パスの記録が読むバックバッファの大きさを控え、ジョブの完了本数を数える JobCounter を用意する。
		m_commandListCount = m_passCount;

		m_backBufferWidth  = device.GetBackBufferWidth();
		m_backBufferHeight = device.GetBackBufferHeight();

		JobCounter recordCounter;

		// ③ Job 指定のパスをジョブへ投入する。
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

		// ④ Main 指定のパスをこちらのスレッドで記録する。
		// 　 ワーカーが Job パスを記録している間に、メインスレッドでしか触れないパスをここで記録する。
		// メイン指定のパスは、ワーカーが走っている間にこちらで積む。
		for (uint32_t passIndex = 0; passIndex < m_passCount; ++passIndex)
		{
			if (m_passes[passIndex].recordThread == EnPassRecordThread::Main)
			{
				RecordPass(passIndex);
			}
		}

		// ⑤ ジョブの完了を待つ。
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


	RenderGraphResourceId RenderGraph::AddResource(
		rhi::EnResourceState initialState,
		rhi::EnResourceState finalState,
		rhi::TextureHandle   texture,
		uint32_t             width,
		uint32_t             height
	)
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

		resource.texture = texture;
		resource.width   = width;
		resource.height  = height;

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
		if (pass.colorTarget.IsValid())
		{
			commandList.SetRenderTargetToBackBuffer(pass.depthTarget.IsValid());
			commandList.SetViewport(m_backBufferWidth, m_backBufferHeight);
		}
		else if (pass.depthTarget.IsValid())
		{
			// 色が無く深度だけのパスはシャドウマップのようなテクスチャ裏付きリソースのみ対応する。
			// デバイス既定の深度バッファは SetRenderTargetToBackBuffer 経由でしか差せない。
			FANG_ASSERT(pass.depthTarget.index < m_resourceCount, "登録していない深度リソースを描画先にしている");

			const Resource& depthResource = m_resources[pass.depthTarget.index];
			FANG_ASSERT(
				depthResource.texture.IsValid(),
				"テクスチャの裏付きが無い深度リソースを色無しで描画先にしている"
			);

			commandList.SetRenderTargetToDepthTexture(depthResource.texture);
			commandList.SetViewport(depthResource.width, depthResource.height);
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
		// バックバッファは専用の遷移口(TransitionBackBuffer)を使う。
		if (barrier.resource.index == m_backBufferResourceId.index)
		{
			commandList.TransitionBackBuffer(barrier.before, barrier.after);
			return;
		}

		// それ以外はテクスチャの裏付きがあるリソースだけ遷移の口(TransitionTexture)を持つ。
		// デバイス既定の深度バッファ(ImportDepthBuffer)は RHI に遷移の口が無いので何もしない。
		if (barrier.resource.index >= m_resourceCount)
		{
			return;
		}

		const Resource& resource = m_resources[barrier.resource.index];
		if (resource.texture.IsValid())
		{
			commandList.TransitionTexture(resource.texture, barrier.before, barrier.after);
		}
	}
} // namespace fang
