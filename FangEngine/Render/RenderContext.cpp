/**
 * @file RenderContext.cpp
 * @brief レンダーコンテキストクラスの実装
 */
#include "RenderContext.h"
#include <iostream>


namespace fangEngine::render
{
	RenderContext::~RenderContext()
	{
		// m_fenceEventは最後に作るため、これがなければGPUに渡した処理もない
		if (m_fenceEvent == nullptr) return;

		WaitForGpu();
		CloseHandle(m_fenceEvent);
	}


	bool RenderContext::Initialize(HWND window)
	{
		if (CreateDevice() == false)                  return false;
		if (CreateCommandQueue() == false)            return false;
		if (CreateSwapChain(window) == false)         return false;
		if (CreateRenderTargetViews() == false)       return false;
		if (CreateCommandAllocatorAndList() == false) return false;
		if (CreateFence() == false)                   return false;

		return true;
	}


	bool RenderContext::BeginFrame()
	{
		// レンダーターゲットビューの取得
		m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle{};
		renderTargetViewDescriptorHandle.ptr = m_renderTargetViewDescriptorHeapHandle.ptr + m_backBufferIndex * m_descriptorHandleIncrementSize;

		// このアロケータのコマンドをGPUが終えるのを待つ。
		// GPUが読んでいる途中にResetすると、コマンドが上書きされるため、Resetの前に待つ。
		if (WaitForFenceValue(m_frameFenceValues[m_backBufferIndex]) == false) return false;

		// コマンドアロケータのリセット
		HRESULT resetCommandAllocatorResult = m_commandAllocators[m_backBufferIndex]->Reset();
		if (FAILED(resetCommandAllocatorResult)) {
			std::cerr << "コマンドアロケータのリセットに失敗しました。" << std::endl;
			return false;
		}
		// コマンドリストのリセット
		HRESULT resetCommandListResult = m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr);
		if (FAILED(resetCommandListResult)) {
			std::cerr << "コマンドリストのリセットに失敗しました。" << std::endl;
			return false;
		}

		// 今描くバッファをPRESENTからRENDER_TARGETへ
		D3D12_RESOURCE_BARRIER resourceBarrier{};
		resourceBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Transition.pResource   = m_swapChainBufferResources[m_backBufferIndex].Get();
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
		m_commandList->ResourceBarrier(1, &resourceBarrier);

		// レンダーターゲットのクリア
		const float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
		m_commandList->ClearRenderTargetView(renderTargetViewDescriptorHandle, clearColor, 0, nullptr);

		// レンダーターゲットビューをセットする
		m_commandList->OMSetRenderTargets(1, &renderTargetViewDescriptorHandle, FALSE, nullptr);

		return true;
	}


	bool RenderContext::EndFrame()
	{
		// 今描いたバッファをRENDER_TARGETからPRESENTへ
		D3D12_RESOURCE_BARRIER resourceBarrier{};
		resourceBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Transition.pResource   = m_swapChainBufferResources[m_backBufferIndex].Get();
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		m_commandList->ResourceBarrier(1, &resourceBarrier);

		// コマンドリストのクローズ
		HRESULT closeCommandListResult = m_commandList->Close();
		if (FAILED(closeCommandListResult)) {
			std::cerr << "コマンドリストのクローズに失敗しました。" << std::endl;
			return false;
		}

		// コマンドリストの実行
		ID3D12CommandList* commandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(1, commandLists);

		// 画面を差し替える
		HRESULT presentResult = m_swapChain->Present(1, 0);
		if (FAILED(presentResult)) {
			std::cerr << "DXGIスワップチェーンの表示に失敗しました。" << std::endl;
			return false;
		}

		// フェンスのシグナル送信
		if (SignalFence() == false) return false;
		// このアロケータの最後のSignalの値を保存する。Resetの前にここまで待つ。
		m_frameFenceValues[m_backBufferIndex] = m_fenceValue;

		return true;
	}


	bool RenderContext::WaitForGpu()
	{
		// m_fenceEventは最後に作るため、これがなければGPUに渡した処理もない
		if (m_fenceEvent == nullptr) return true;

		if (SignalFence() == false) return false;

		return WaitForFenceValue(m_fenceValue);
	}


	bool RenderContext::CreateDevice()
	{
		HRESULT createDeviceResult = D3D12CreateDevice(
			nullptr,
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		);
		if (FAILED(createDeviceResult)) {
			std::cerr << "D3D12デバイスの作成に失敗しました。" << std::endl;
			return false;
		}
		return true;
	}


	bool RenderContext::CreateCommandQueue()
	{
		D3D12_COMMAND_QUEUE_DESC commandQueueDescription{};
		commandQueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		HRESULT createCommandQueueResult = m_device->CreateCommandQueue(
			&commandQueueDescription,
			IID_PPV_ARGS(&m_commandQueue)
		);
		if (FAILED(createCommandQueueResult)) {
			std::cerr << "D3D12コマンドキューの作成に失敗しました。" << std::endl;
			return false;
		}
		return true;
	}


	bool RenderContext::CreateSwapChain(HWND window)
	{
		// DXGIファクトリの作成
		Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
		HRESULT createFactoryResult = CreateDXGIFactory2(
			0,
			IID_PPV_ARGS(&factory)
		);
		if (FAILED(createFactoryResult)) {
			std::cerr << "DXGIファクトリの作成に失敗しました。" << std::endl;
			return false;
		}

		// DXGIスワップチェーンの作成
		// ウィンドウ全体ではなくクライアント領域の大きさで作るため、GetClientRectで取得する
		RECT clientRectangle{};
		GetClientRect(window, &clientRectangle);

		DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
		swapChainDescription.Width            = clientRectangle.right - clientRectangle.left;
		swapChainDescription.Height           = clientRectangle.bottom - clientRectangle.top;
		swapChainDescription.Format           = RENDER_TARGET_FORMAT;
		swapChainDescription.SampleDesc.Count = 1;
		swapChainDescription.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDescription.BufferCount      = RENDER_TARGET_COUNT;
		swapChainDescription.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		Microsoft::WRL::ComPtr<IDXGISwapChain1> createdSwapChain;
		HRESULT createSwapChainResult = factory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),
			window,
			&swapChainDescription,
			nullptr,
			nullptr,
			&createdSwapChain
		);
		if (FAILED(createSwapChainResult)) {
			std::cerr << "DXGIスワップチェーンの作成に失敗しました。" << std::endl;
			return false;
		}

		// CreateSwapChainForHwndの引数はIDXGISwapChain1だが、GetCurrentBackBufferIndexを使うためにIDXGISwapChain3に変換する
		HRESULT asResult = createdSwapChain.As(&m_swapChain);
		if (FAILED(asResult)) {
			std::cerr << "DXGIスワップチェーンの変換に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}


	bool RenderContext::CreateRenderTargetViews()
	{
		// レンダーターゲットビュー用のディスクリプタヒープの作成
		D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewDescriptorHeapDescription{};
		renderTargetViewDescriptorHeapDescription.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		renderTargetViewDescriptorHeapDescription.NumDescriptors = RENDER_TARGET_COUNT;
		renderTargetViewDescriptorHeapDescription.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT createDescriptorHeapResult = m_device->CreateDescriptorHeap(
			&renderTargetViewDescriptorHeapDescription,
			IID_PPV_ARGS(&m_renderTargetViewDescriptorHeap)
		);
		if (FAILED(createDescriptorHeapResult)) {
			std::cerr << "レンダーターゲットビュー用のディスクリプタヒープの作成に失敗しました。" << std::endl;
			return false;
		}

		// レンダーターゲットビューの作成
		m_renderTargetViewDescriptorHeapHandle = m_renderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_descriptorHandleIncrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		for (int renderTargetIndex = 0; renderTargetIndex < RENDER_TARGET_COUNT; ++renderTargetIndex)
		{
			HRESULT getBufferResult = m_swapChain->GetBuffer(renderTargetIndex, IID_PPV_ARGS(&m_swapChainBufferResources[renderTargetIndex]));
			if (FAILED(getBufferResult)) {
				std::cerr << "スワップチェーンのバッファの取得に失敗しました。" << std::endl;
				return false;
			}
			m_device->CreateRenderTargetView(
				m_swapChainBufferResources[renderTargetIndex].Get(),
				nullptr,
				D3D12_CPU_DESCRIPTOR_HANDLE{ m_renderTargetViewDescriptorHeapHandle.ptr + renderTargetIndex * m_descriptorHandleIncrementSize }
			);
		}

		return true;
	}


	bool RenderContext::CreateCommandAllocatorAndList()
	{
		for (int allocatorIndex = 0; allocatorIndex < RENDER_TARGET_COUNT; ++allocatorIndex)
		{
			// コマンドアロケータの作成
			HRESULT createCommandAllocatorResult = m_device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(&m_commandAllocators[allocatorIndex])
			);
			if (FAILED(createCommandAllocatorResult)) {
				std::cerr << "コマンドアロケータの作成に失敗しました。" << std::endl;
				return false;
			}
		}

		// コマンドリストの作成
		HRESULT createCommandListResult = m_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocators[0].Get(),
			nullptr,
			IID_PPV_ARGS(&m_commandList)
		);
		if (FAILED(createCommandListResult)) {
			std::cerr << "コマンドリストの作成に失敗しました。" << std::endl;
			return false;
		}
		// 作成直後のリストは書き込み中であり、その最中にリセットを呼ぶとエラーになるため、Closeを呼んで書き込み中を解除する
		HRESULT closeCommandListResult = m_commandList->Close();
		if (FAILED(closeCommandListResult)) {
			std::cerr << "コマンドリストのクローズに失敗しました。" << std::endl;
			return false;
		}

		return true;
	}


	bool RenderContext::CreateFence()
	{
		HRESULT createFenceResult = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		if (FAILED(createFenceResult)) {
			std::cerr << "フェンスの作成に失敗しました。" << std::endl;
			return false;
		}
		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr) {
			std::cerr << "フェンスイベントの作成に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}


	bool RenderContext::SignalFence()
	{
		// 新しい番号を発行して、Queueの最後に積む。GPUが前の処理を全部終えるとここへ届く。
		m_fenceValue++;
		HRESULT signalResult = m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
		if (FAILED(signalResult)) {
			std::cerr << "フェンスのシグナル送信に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}


	bool RenderContext::WaitForFenceValue(UINT64 fenceValue)
	{
		// GPUがもう届いているなら待たない
		if (m_fence->GetCompletedValue() >= fenceValue) return true;

		// 届いたらm_fenceEventを立てるよう頼む
		HRESULT setEventOnCompletionResult = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
		if (FAILED(setEventOnCompletionResult)) {
			std::cerr << "フェンスのイベント設定に失敗しました。" << std::endl;
			return false;
		}

		// 合図が立つまでCPUを止める
		WaitForSingleObject(m_fenceEvent, INFINITE);

		return true;
	}
} // namespace fangEngine::render
