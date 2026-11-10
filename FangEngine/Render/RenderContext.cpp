/**
 * @file RenderContext.cpp
 * @brief レンダーコンテキストクラスの実装
 */
#include "RenderContext.h"
#include "Core/Log.h"


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
		if (CreateDepthStencilView() == false)        return false;
		if (CreateCommandAllocatorAndList() == false) return false;
		if (CreateFence() == false)                   return false;

		if (m_gpuProfiler.Initialize(m_device.Get(), m_commandQueue.Get()) == false) return false;

		return true;
	}


	bool RenderContext::BeginFrame()
	{
		// 今描くバッファのインデックスを取得する
		m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

		// レンダーターゲットビューの取得
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle{};
		renderTargetViewDescriptorHandle.ptr = m_renderTargetViewDescriptorHeapHandle.ptr + m_backBufferIndex * m_descriptorHandleIncrementSize;
		// デプスステンシルビューの取得
		D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewDescriptorHandle = m_depthStencilViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

		// このアロケータのコマンドをGPUが終えるのを待つ。
		// GPUが読んでいる途中にResetすると、コマンドが上書きされるため、Resetの前に待つ。
		if (WaitForFenceValue(m_frameFenceValues[m_backBufferIndex]) == false) return false;

		// 待ちを除いたCPUの処理時間を測り始める
		m_cpuProfiler.BeginFrame();

		// コマンドアロケータのリセット
		HRESULT resetCommandAllocatorResult = m_commandAllocators[m_backBufferIndex]->Reset();
		if (core::LogIfFailed(resetCommandAllocatorResult, L"コマンドアロケータのリセットに失敗しました。")) return false;
		// コマンドリストのリセット
		HRESULT resetCommandListResult = m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr);
		if (core::LogIfFailed(resetCommandListResult, L"コマンドリストのリセットに失敗しました。")) return false;

		// 2フレーム前の結果を読み、このフレームのGPU処理の計測を始める
		m_gpuProfiler.BeginFrame(m_commandList.Get(), m_backBufferIndex);

		// 今描くバッファをPRESENTからRENDER_TARGETへ
		TransitionBackBuffer(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

		// レンダーターゲットのクリア
		const float clearColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
		m_commandList->ClearRenderTargetView(renderTargetViewDescriptorHandle, clearColor, 0, nullptr);
		// デプスステンシルバッファのクリア
		m_commandList->ClearDepthStencilView(depthStencilViewDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		// レンダーターゲットと深度バッファをセットする
		m_commandList->OMSetRenderTargets(1, &renderTargetViewDescriptorHandle, FALSE, &depthStencilViewDescriptorHandle);

		// ビューポートとシザー矩形をセットする。
		// Resetで設定が消えるため、毎フレームセットする。
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRectangle);

		return true;
	}


	bool RenderContext::EndFrame()
	{
		// 今描いたバッファをRENDER_TARGETからPRESENTへ
		TransitionBackBuffer(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

		// GPUの処理時間を測り終える
		m_gpuProfiler.EndFrame(m_commandList.Get(), m_backBufferIndex);

		// コマンドリストのクローズ
		HRESULT closeCommandListResult = m_commandList->Close();
		if (core::LogIfFailed(closeCommandListResult, L"コマンドリストのクローズに失敗しました。")) return false;

		// コマンドリストの実行
		ID3D12CommandList* commandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(1, commandLists);

		// 画面の待ちが入らないように、Presentの前で測り終える
		m_cpuProfiler.EndFrame();

		// 画面を差し替える
		HRESULT presentResult = m_swapChain->Present(1, 0);
		if (core::LogIfFailed(presentResult, L"DXGIスワップチェーンの表示に失敗しました。")) return false;

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
#ifdef _DEBUG
		// デバッグレイヤーの有効化
		// デバイスを作ったあとに呼ぶと、そのデバイスが無効になるため、デバイスより先に有効にする
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		HRESULT getDebugInterfaceResult = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
		if (core::LogIfFailed(getDebugInterfaceResult, L"デバッグレイヤーの取得に失敗しました。")) return false;
		debugController->EnableDebugLayer();
#endif // _DEBUG

		HRESULT createDeviceResult = D3D12CreateDevice(
			nullptr,
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		);
		if (core::LogIfFailed(createDeviceResult, L"D3D12デバイスの作成に失敗しました。")) return false;
		return true;
	}


	bool RenderContext::CreateCommandQueue()
	{
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		HRESULT result = m_device->CreateCommandQueue(
			&commandQueueDesc,
			IID_PPV_ARGS(&m_commandQueue)
		);
		if (core::LogIfFailed(result, L"D3D12コマンドキューの作成に失敗しました。")) return false;
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
		if (core::LogIfFailed(createFactoryResult, L"DXGIファクトリの作成に失敗しました。")) return false;

		// DXGIスワップチェーンの作成
		// ウィンドウ全体ではなくクライアント領域の大きさで作るため、GetClientRectで取得する
		RECT clientRectangle{};
		GetClientRect(window, &clientRectangle);

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width            = clientRectangle.right - clientRectangle.left;
		swapChainDesc.Height           = clientRectangle.bottom - clientRectangle.top;
		swapChainDesc.Format           = RENDER_TARGET_FORMAT;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount      = RENDER_TARGET_COUNT;
		swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		Microsoft::WRL::ComPtr<IDXGISwapChain1> createdSwapChain;
		HRESULT createSwapChainResult = factory->CreateSwapChainForHwnd(
			m_commandQueue.Get(),
			window,
			&swapChainDesc,
			nullptr,
			nullptr,
			&createdSwapChain
		);
		if (core::LogIfFailed(createSwapChainResult, L"DXGIスワップチェーンの作成に失敗しました。")) return false;

		// CreateSwapChainForHwndの引数はIDXGISwapChain1だが、GetCurrentBackBufferIndexを使うためにIDXGISwapChain3に変換する
		HRESULT asResult = createdSwapChain.As(&m_swapChain);
		if (core::LogIfFailed(asResult, L"DXGIスワップチェーンの変換に失敗しました。")) return false;

		// ビューポートとシザー矩形の設定
		// ビューポート : 頂点の座標(-1〜1)を、描き先のどのピクセルの範囲に広げるか。
		// シザー : その中で、どの四角の外を描かずに切るか。
		m_viewport.Width          = static_cast<float>(swapChainDesc.Width);
		m_viewport.Height         = static_cast<float>(swapChainDesc.Height);
		m_viewport.MaxDepth       = 1.0f;
		m_scissorRectangle.right  = static_cast<LONG>(swapChainDesc.Width);
		m_scissorRectangle.bottom = static_cast<LONG>(swapChainDesc.Height);

		return true;
	}


	bool RenderContext::CreateRenderTargetViews()
	{
		// レンダーターゲットビュー用のディスクリプタヒープの作成
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		descriptorHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		descriptorHeapDesc.NumDescriptors = RENDER_TARGET_COUNT;
		descriptorHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT createDescriptorHeapResult = m_device->CreateDescriptorHeap(
			&descriptorHeapDesc,
			IID_PPV_ARGS(&m_renderTargetViewDescriptorHeap)
		);
		if (core::LogIfFailed(createDescriptorHeapResult, L"レンダーターゲットビュー用のディスクリプタヒープの作成に失敗しました。")) return false;

		// レンダーターゲットビューの作成
		m_renderTargetViewDescriptorHeapHandle = m_renderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		m_descriptorHandleIncrementSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		for (int renderTargetIndex = 0; renderTargetIndex < RENDER_TARGET_COUNT; ++renderTargetIndex)
		{
			HRESULT getBufferResult = m_swapChain->GetBuffer(renderTargetIndex, IID_PPV_ARGS(&m_swapChainBufferResources[renderTargetIndex]));
			if (core::LogIfFailed(getBufferResult, L"スワップチェーンのバッファの取得に失敗しました。")) return false;
			m_device->CreateRenderTargetView(
				m_swapChainBufferResources[renderTargetIndex].Get(),
				nullptr,
				D3D12_CPU_DESCRIPTOR_HANDLE{ m_renderTargetViewDescriptorHeapHandle.ptr + renderTargetIndex * m_descriptorHandleIncrementSize }
			);
		}

		return true;
	}


	bool RenderContext::CreateDepthStencilView()
	{
		// 深度バッファをバックバッファと同じ大きさで作るため、スワップチェーンの設定を読む
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		HRESULT getSwapChainDescResult = m_swapChain->GetDesc1(&swapChainDesc);
		if (core::LogIfFailed(getSwapChainDescResult, L"スワップチェーンの記述の取得に失敗しました。")) return false;

		// デプスステンシルビュー用のディスクリプタヒープの作成
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		descriptorHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		descriptorHeapDesc.NumDescriptors = 1;
		descriptorHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT createDescriptorHeapResult = m_device->CreateDescriptorHeap(
			&descriptorHeapDesc,
			IID_PPV_ARGS(&m_depthStencilViewDescriptorHeap)
		);
		if (core::LogIfFailed(createDescriptorHeapResult, L"デプスステンシルビュー用のディスクリプタヒープの作成に失敗しました。")) return false;

		// デプスステンシルバッファの作成
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width            = swapChainDesc.Width;
		resourceDesc.Height           = swapChainDesc.Height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels        = 1;
		resourceDesc.Format           = DEPTH_STENCIL_FORMAT;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format             = DEPTH_STENCIL_FORMAT;
		clearValue.DepthStencil.Depth = 1.0f;

		HRESULT createCommittedResourceResult = m_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(&m_depthStencilBufferResource)
		);
		if (core::LogIfFailed(createCommittedResourceResult, L"デプスステンシルバッファの作成に失敗しました。")) return false;

		// デプスステンシルビューの作成
		m_device->CreateDepthStencilView(
			m_depthStencilBufferResource.Get(),
			nullptr,
			m_depthStencilViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart()
		);

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
			if (core::LogIfFailed(createCommandAllocatorResult, L"コマンドアロケータの作成に失敗しました。")) return false;
		}

		// コマンドリストの作成
		HRESULT createCommandListResult = m_device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			m_commandAllocators[0].Get(),
			nullptr,
			IID_PPV_ARGS(&m_commandList)
		);
		if (core::LogIfFailed(createCommandListResult, L"コマンドリストの作成に失敗しました。")) return false;
		// 作成直後のリストは書き込み中であり、その最中にリセットを呼ぶとエラーになるため、Closeを呼んで書き込み中を解除する
		HRESULT closeCommandListResult = m_commandList->Close();
		if (core::LogIfFailed(closeCommandListResult, L"コマンドリストのクローズに失敗しました。")) return false;

		return true;
	}


	bool RenderContext::CreateFence()
	{
		HRESULT result = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		if (core::LogIfFailed(result, L"フェンスの作成に失敗しました。")) return false;
		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr) {
			core::LogError(L"フェンスイベントの作成に失敗しました。");
			return false;
		}

		return true;
	}


	bool RenderContext::SignalFence()
	{
		// 新しい番号を発行して、Queueの最後に積む。GPUが前の処理を全部終えるとここへ届く。
		m_fenceValue++;
		HRESULT result = m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
		if (core::LogIfFailed(result, L"フェンスのシグナル送信に失敗しました。")) return false;

		return true;
	}


	bool RenderContext::WaitForFenceValue(UINT64 fenceValue)
	{
		// GPUがもう届いているなら待たない
		if (m_fence->GetCompletedValue() >= fenceValue) return true;

		// 届いたらm_fenceEventを立てるよう頼む
		HRESULT result = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
		if (core::LogIfFailed(result, L"フェンスのイベント設定に失敗しました。")) return false;

		// 合図が立つまでCPUを止める
		WaitForSingleObject(m_fenceEvent, INFINITE);

		return true;
	}


	void RenderContext::TransitionBackBuffer(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER resourceBarrier{};
		resourceBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Transition.pResource   = m_swapChainBufferResources[m_backBufferIndex].Get();
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resourceBarrier.Transition.StateBefore = before;
		resourceBarrier.Transition.StateAfter  = after;
		m_commandList->ResourceBarrier(1, &resourceBarrier);
	}
} // namespace fangEngine::render
