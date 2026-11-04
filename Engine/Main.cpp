/**
 * @file Main.cpp
 * @brief メイン関数の実装
 */
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include "Core/Memory/Memory.h"


namespace
{
	constexpr const wchar_t* WINDOW_CLASS_NAME   = L"FangsOfRagnarok_MainWindow"; // ウィンドウクラス名
	constexpr const wchar_t* WINDOW_TITLE        = L"Fangs of Ragnarok";          // ウィンドウ上部に表示されるタイトル
	constexpr int            WINDOW_WIDTH        = 800;
	constexpr int            WINDOW_HEIGHT       = 600;
	constexpr int            RENDER_TARGET_COUNT = 2;

	constexpr float MAX_DELTA_TIME = 1.0f / 30.0f;

	/**
	 * @brief WM_DESTROYの場合はループを抜ける指示を出し、それ以外の場合はWindowsの既定の処理に任せる
	 * @return WM_DESTROYの場合は0を返し、それ以外の場合はDefWindowProcWの戻り値を返す
	 */
	LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(window, message, wParam, lParam);
		}
	}
}


int main()
{
	//==========================================================
	// ウィンドウの作成
	//==========================================================
	HINSTANCE instance = GetModuleHandleW(nullptr);
	WNDCLASSEXW windowClass{};
	windowClass.cbSize        = sizeof(WNDCLASSEXW);
	windowClass.lpfnWndProc   = WindowProcedure;
	windowClass.hInstance     = instance;
	windowClass.lpszClassName = WINDOW_CLASS_NAME;
	ATOM atom = RegisterClassExW(&windowClass);

	if (atom == 0) {
		std::cerr << "ウィンドウクラスの登録に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	HWND window = CreateWindowExW(
		0,
		WINDOW_CLASS_NAME,
		WINDOW_TITLE,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WINDOW_WIDTH,
		WINDOW_HEIGHT,
		nullptr,
		nullptr,
		instance,
		nullptr
	);

	if (window == nullptr) {
		std::cerr << "ウィンドウの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	ShowWindow(window, SW_SHOW);


	//==========================================================
	// 描画処理
	//==========================================================
	// D3D12デバイスの作成
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	HRESULT createDeviceResult = D3D12CreateDevice(
		nullptr,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&device)
	);

	if (FAILED(createDeviceResult)) {
		std::cerr << "D3D12デバイスの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// D3D12コマンドキューの作成
	D3D12_COMMAND_QUEUE_DESC commandQueueDescription{};
	commandQueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
	HRESULT createCommandQueueResult = device->CreateCommandQueue(
		&commandQueueDescription,
		IID_PPV_ARGS(&commandQueue)
	);

	if (FAILED(createCommandQueueResult)) {
		std::cerr << "D3D12コマンドキューの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// DXGIファクトリの作成
	Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
	HRESULT createFactoryResult = CreateDXGIFactory2(
		0,
		IID_PPV_ARGS(&factory)
	);

	if (FAILED(createFactoryResult)) {
		std::cerr << "DXGIファクトリの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// DXGIスワップチェーンの作成
	// ※WINDOW_WIDTHとWINDOW_HEIGHTはウィンドウ全体のサイズであり、クライアント領域のサイズではないため、GetClientRectで取得する
	RECT clientRectangle{};
	GetClientRect(window, &clientRectangle);

	DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
	swapChainDescription.Width            = clientRectangle.right - clientRectangle.left;
	swapChainDescription.Height           = clientRectangle.bottom - clientRectangle.top;
	swapChainDescription.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDescription.SampleDesc.Count = 1;
	swapChainDescription.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDescription.BufferCount      = RENDER_TARGET_COUNT;
	swapChainDescription.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> createdSwapChain;
	HRESULT createSwapChainResult = factory->CreateSwapChainForHwnd(
		commandQueue.Get(),
		window,
		&swapChainDescription,
		nullptr,
		nullptr,
		&createdSwapChain
	);
	if (FAILED(createSwapChainResult)) {
		std::cerr << "DXGIスワップチェーンの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// CreateSwapChainForHwndの引数はIDXGISwapChain1だが、GetCurrentBackBufferIndexを使うためにIDXGISwapChain3に変換する
	Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
	HRESULT asResult = createdSwapChain.As(&swapChain);
	if (FAILED(asResult)) {
		std::cerr << "DXGIスワップチェーンの変換に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// レンダーターゲットビュー用のディスクリプタヒープの作成
	D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewDescriptorHeapDescription{};
	renderTargetViewDescriptorHeapDescription.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	renderTargetViewDescriptorHeapDescription.NumDescriptors = RENDER_TARGET_COUNT;
	renderTargetViewDescriptorHeapDescription.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewDescriptorHeap;

	HRESULT createDescriptorHeapResult = device->CreateDescriptorHeap(
		&renderTargetViewDescriptorHeapDescription,
		IID_PPV_ARGS(&renderTargetViewDescriptorHeap)
	);

	if (FAILED(createDescriptorHeapResult)) {
		std::cerr << "レンダーターゲットビュー用のディスクリプタヒープの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// レンダーターゲットビューの作成
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainBufferResources[RENDER_TARGET_COUNT];
	D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHeapHandle = renderTargetViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	UINT descriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (int renderTargetIndex = 0; renderTargetIndex < RENDER_TARGET_COUNT; ++renderTargetIndex)
	{
		HRESULT getBufferResult = swapChain->GetBuffer(renderTargetIndex, IID_PPV_ARGS(&swapChainBufferResources[renderTargetIndex]));
		if (FAILED(getBufferResult)) {
			std::cerr << "スワップチェーンのバッファの取得に失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}
		device->CreateRenderTargetView(
			swapChainBufferResources[renderTargetIndex].Get(),
			nullptr,
			D3D12_CPU_DESCRIPTOR_HANDLE{ renderTargetViewDescriptorHeapHandle.ptr + renderTargetIndex * descriptorHandleIncrementSize }
		);
	}

	// コマンドアロケータの作成
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	HRESULT createCommandAllocatorResult = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	if (FAILED(createCommandAllocatorResult)) {
		std::cerr << "コマンドアロケータの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// コマンドリストの作成
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	HRESULT createCommandListResult = device->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator.Get(),
		nullptr,
		IID_PPV_ARGS(&commandList)
	);
	if (FAILED(createCommandListResult)) {
		std::cerr << "コマンドリストの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}
	// 作成直後のリストは書き込み中であり、その最中にリセットを呼ぶとエラーになるため、Closeを呼んで書き込み中を解除する
	HRESULT closeCommandListResult = commandList->Close();
	if (FAILED(closeCommandListResult)) {
		std::cerr << "コマンドリストのクローズに失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}

	// フェンスの作成
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	HRESULT createFenceResult = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	if (FAILED(createFenceResult)) {
		std::cerr << "フェンスの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}
	UINT64 fenceValue = 0;
	HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr) {
		std::cerr << "フェンスイベントの作成に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}


	//==========================================================
	// メインループ
	//==========================================================
	MSG message{};
	auto previousFrameTime = std::chrono::steady_clock::now();
	bool running = true;
	while (running)
	{
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT) {
				running = false;
				break;
			}
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		// ウィンドウが閉じられた場合はループを抜ける
		if (running == false) break;

		// 時間の更新処理
		auto currentFrameTime = std::chrono::steady_clock::now();
		float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
		if (deltaTime > MAX_DELTA_TIME) {
			deltaTime = MAX_DELTA_TIME;
		}
		previousFrameTime = currentFrameTime;

		// レンダーターゲットビューの取得
		UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetViewDescriptorHandle{};
		renderTargetViewDescriptorHandle.ptr = renderTargetViewDescriptorHeapHandle.ptr + backBufferIndex * descriptorHandleIncrementSize;

		// コマンドアロケータのリセット
		HRESULT resetCommandAllocatorResult = commandAllocator->Reset();
		if (FAILED(resetCommandAllocatorResult)) {
			std::cerr << "コマンドアロケータのリセットに失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}
		// コマンドリストのリセット
		HRESULT resetCommandListResult = commandList->Reset(commandAllocator.Get(), nullptr);
		if (FAILED(resetCommandListResult)) {
			std::cerr << "コマンドリストのリセットに失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}

		// 今描くバッファをPRESENTからRENDER_TARGETへ
		D3D12_RESOURCE_BARRIER resourceBarrier{};
		resourceBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Transition.pResource   = swapChainBufferResources[backBufferIndex].Get();
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
		commandList->ResourceBarrier(1, &resourceBarrier);

		// レンダーターゲットのクリア
		const float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
		commandList->ClearRenderTargetView(renderTargetViewDescriptorHandle, clearColor, 0, nullptr);

		// 今描いたバッファをRENDER_TARGETからPRESENTへ
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		commandList->ResourceBarrier(1, &resourceBarrier);

		// コマンドリストのクローズ
		HRESULT closeCommandListResult = commandList->Close();
		if (FAILED(closeCommandListResult)) {
			std::cerr << "コマンドリストのクローズに失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}

		// コマンドリストの実行
		ID3D12CommandList* commandLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(1, commandLists);

		// 画面を差し替える
		HRESULT presentResult = swapChain->Present(1, 0);
		if (FAILED(presentResult)) {
			std::cerr << "DXGIスワップチェーンの表示に失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}

		// フェンスのシグナルとイベントの設定
		fenceValue++;
		HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
		if (FAILED(signalResult)) {
			std::cerr << "フェンスのシグナルに失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}
		HRESULT setEventOnCompletionResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		if (FAILED(setEventOnCompletionResult)) {
			std::cerr << "フェンスのイベント設定に失敗しました。" << std::endl;
			return EXIT_FAILURE;
		}
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// ウィンドウを閉じたときに、GPUの処理が完了するまで待機する。
	// mainを抜けるとComPtrがキューやバッファを解放するため、GPUが使っている途中で解放しないようにする。
	fenceValue++;
	HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
	if (FAILED(signalResult)) {
		std::cerr << "フェンスのシグナルに失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}
	HRESULT setEventOnCompletionResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
	if (FAILED(setEventOnCompletionResult)) {
		std::cerr << "フェンスのイベント設定に失敗しました。" << std::endl;
		return EXIT_FAILURE;
	}
	WaitForSingleObject(fenceEvent, INFINITE);

	CloseHandle(fenceEvent);


	core::Memory memory;
	void* ptr1 = memory.MallocMemory(64);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	void* ptr2 = memory.MallocMemory(32);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	memory.FreeMemory(ptr1);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	memory.FreeMemory(ptr2);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	return 0;
}
