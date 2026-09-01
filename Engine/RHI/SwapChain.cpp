/**
 * @file SwapChain.cpp
 * @brief バックバッファの束と、その描画先ビューの実装。
 */
#include "Pch.h"
#include "RHI/SwapChain.h"


namespace fang::rhi
{
	bool SwapChain::Initialize(IDXGIFactory6&      factory,
							   ID3D12Device&       device,
							   ID3D12CommandQueue& commandQueue,
							   void*               windowHandle,
							   uint32_t            width,
							   uint32_t            height)
	{
		m_width  = width;
		m_height = height;

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.BufferCount      = BACK_BUFFER_COUNT;
		swapChainDesc.Width            = width;
		swapChainDesc.Height           = height;
		swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;

		ComPtr<IDXGISwapChain1> swapChain;
#if FANG_PLATFORM_WINDOWS
		const HRESULT swapChainResult = factory.CreateSwapChainForHwnd(&commandQueue,
																	   static_cast<HWND>(windowHandle),
																	   &swapChainDesc,
																	   nullptr,
																	   nullptr,
																	   &swapChain);
#else
		// UWP は CoreWindow を IUnknown* にして渡す。
		const HRESULT swapChainResult = factory.CreateSwapChainForCoreWindow(&commandQueue,
																			 static_cast<IUnknown*>(windowHandle),
																			 &swapChainDesc,
																			 nullptr,
																			 &swapChain);
#endif
		if (!CheckHresult(swapChainResult, "スワップチェーンの生成"))
		{
			return false;
		}

		if (!CheckHresult(swapChain.As(&m_swapChain), "スワップチェーンの問い合わせ"))
		{
			return false;
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewHeapDesc{};
		renderTargetViewHeapDesc.NumDescriptors = BACK_BUFFER_COUNT;
		renderTargetViewHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if (!CheckHresult(device.CreateDescriptorHeap(&renderTargetViewHeapDesc, IID_PPV_ARGS(&m_renderTargetViewHeap)),
						  "レンダーターゲットビューのヒープ生成"))
		{
			return false;
		}

		m_renderTargetViewSize = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		return CreateRenderTargetViews(device);
	}

	void SwapChain::Resize(ID3D12Device& device, uint32_t width, uint32_t height)
	{
		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			m_backBuffers[bufferIndex].Reset();
		}

		if (!CheckHresult(m_swapChain->ResizeBuffers(BACK_BUFFER_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
						  "スワップチェーンのリサイズ"))
		{
			return;
		}

		m_width      = width;
		m_height     = height;
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		if (!CreateRenderTargetViews(device))
		{
			return;
		}

		FANG_LOG_INFO(RHI, "バックバッファを作り直した ({}x{})", width, height);
	}

	void SwapChain::Shutdown()
	{
		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			m_backBuffers[bufferIndex].Reset();
		}

		m_renderTargetViewHeap.Reset();
		m_swapChain.Reset();
	}

	void SwapChain::Present()
	{
		FANG_VERIFY(SUCCEEDED(m_swapChain->Present(1, 0)));
	}

	void SwapChain::UpdateFrameIndex()
	{
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::GetCurrentRenderTargetView() const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<SIZE_T>(m_frameIndex) * m_renderTargetViewSize;
		return handle;
	}

	bool SwapChain::CreateRenderTargetViews(ID3D12Device& device)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			if (!CheckHresult(m_swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&m_backBuffers[bufferIndex])),
							  "バックバッファの取得"))
			{
				return false;
			}

			device.CreateRenderTargetView(m_backBuffers[bufferIndex].Get(), nullptr, renderTargetView);
			renderTargetView.ptr += m_renderTargetViewSize;
		}

		return true;
	}
} // namespace fang::rhi
