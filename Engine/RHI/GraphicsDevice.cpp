/**
 * @file GraphicsDevice.cpp
 * @brief DirectX 12 のデバイス・スワップチェーン・コマンド記録の実装。
 */
#include "Pch.h"
#include "RHI/GraphicsDevice.h"
#include "Core/Log/Assert.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

FANG_DEFINE_LOG_CATEGORY(RHI);


namespace fang::rhi
{
	namespace
	{
		const char* ToDisplayName(D3D_FEATURE_LEVEL featureLevel)
		{
			switch (featureLevel)
			{
				case D3D_FEATURE_LEVEL_12_2: return "12_2";
				case D3D_FEATURE_LEVEL_12_1: return "12_1";
				case D3D_FEATURE_LEVEL_12_0: return "12_0";
				case D3D_FEATURE_LEVEL_11_1: return "11_1";
				case D3D_FEATURE_LEVEL_11_0: return "11_0";
				default: break;
			}

			return "不明";
		}
	} // namespace

	GraphicsDevice::GraphicsDevice() = default;

	GraphicsDevice::~GraphicsDevice()
	{
		Shutdown();
	}

	bool GraphicsDevice::Initialize(const GraphicsDeviceDesc& desc)
	{
		FANG_ASSERT(!m_isInitialized, "GraphicsDevice を二重に初期化している");
		FANG_ASSERT(desc.windowHandle != nullptr, "ウィンドウのハンドルが無い");

		// 以降で失敗しても、ここまで作った分は Shutdown が片付ける。
		m_isInitialized = true;

		UINT factoryFlags = 0;
		if (desc.isDebugLayerEnabled)
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
				FANG_LOG_INFO(RHI, "D3D12 のデバッグレイヤーを有効にした");
			}
			else
			{
				FANG_LOG_WARNING(RHI, "D3D12 のデバッグレイヤーを取れなかった");
			}
		}

		if (!CheckHresult(::CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "DXGI ファクトリの生成"))
		{
			return false;
		}

		ComPtr<IDXGIAdapter1> adapter;
		for (UINT adapterIndex = 0; m_factory->EnumAdapterByGpuPreference(
										adapterIndex,
										DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
										IID_PPV_ARGS(&adapter)
									) != DXGI_ERROR_NOT_FOUND;
			 ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 adapterDesc{};
			FANG_VERIFY(SUCCEEDED(adapter->GetDesc1(&adapterDesc)));
			if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			{
				adapter.Reset();
				continue;
			}

			if (SUCCEEDED(::D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
			{
				break;
			}

			adapter.Reset();
		}

		if (m_device == nullptr)
		{
			FANG_LOG_ERROR(RHI, "D3D12 デバイスを作れなかった");
			return false;
		}

		// 起動直後に生成の成否と Feature Level を残す。App 分類で配置した事故に早く気付くため。
		// clang-format off
		constexpr D3D_FEATURE_LEVEL CANDIDATE_FEATURE_LEVELS[] = {
			D3D_FEATURE_LEVEL_12_2,
			D3D_FEATURE_LEVEL_12_1,
			D3D_FEATURE_LEVEL_12_0,
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		// clang-format on
		D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
		featureLevels.NumFeatureLevels        = FANG_COUNT_OF(CANDIDATE_FEATURE_LEVELS);
		featureLevels.pFeatureLevelsRequested = CANDIDATE_FEATURE_LEVELS;
		if (SUCCEEDED(
				m_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels))
			))
		{
			FANG_LOG_INFO(
				RHI,
				"D3D12 デバイスを作った (Feature Level {})",
				ToDisplayName(featureLevels.MaxSupportedFeatureLevel)
			);
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		if (!CheckHresult(
				m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)),
				"コマンドキューの生成"
			))
		{
			return false;
		}

		if (!m_swapChain.Initialize(
				*m_factory.Get(),
				*m_device.Get(),
				*m_commandQueue.Get(),
				desc.windowHandle,
				desc.width,
				desc.height
			))
		{
			return false;
		}

		if (!m_shaderVisibleHeap.Initialize(*m_device.Get()))
		{
			return false;
		}

		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			if (!CheckHresult(
					m_device->CreateCommandAllocator(
						D3D12_COMMAND_LIST_TYPE_DIRECT,
						IID_PPV_ARGS(&m_commandAllocators[bufferIndex])
					),
					"コマンドアロケータの生成"
				))
			{
				return false;
			}
		}

		if (!CheckHresult(
				m_device->CreateCommandList(
					0,
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					m_commandAllocators[m_swapChain.GetFrameIndex()].Get(),
					nullptr,
					IID_PPV_ARGS(&m_commandList)
				),
				"コマンドリストの生成"
			))
		{
			return false;
		}

		FANG_VERIFY(SUCCEEDED(m_commandList->Close()));

		if (!m_fence.Initialize(*m_device.Get()))
		{
			return false;
		}

		m_commandListWrapper.m_device = this;

		return true;
	}

	void GraphicsDevice::Shutdown()
	{
		if (!m_isInitialized)
		{
			return;
		}

		// 初期化が途中で失敗しているとキューが無い。積んだ仕事も無いので待たずに片付ける。
		if (m_commandQueue != nullptr)
		{
			m_fence.WaitForGPU(*m_commandQueue.Get());
		}

		// 生成と逆の順に手放す。
		m_commandListWrapper = {};
		m_commandList.Reset();
		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			m_commandAllocators[bufferIndex].Reset();
		}

		m_textures.Shutdown();
		m_buffers.Shutdown();
		m_pipelines.Shutdown();

		m_fence.Shutdown();
		m_shaderVisibleHeap.Shutdown();
		m_swapChain.Shutdown();

		m_commandQueue.Reset();
		m_device.Reset();
		m_factory.Reset();

		m_isFrameOpen   = false;
		m_isInitialized = false;
	}

	PipelineHandle GraphicsDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		return m_pipelines.Create(*m_device.Get(), desc);
	}

	void GraphicsDevice::DestroyPipeline(PipelineHandle handle)
	{
		m_pipelines.Destroy(handle);
	}

	BufferHandle GraphicsDevice::CreateBuffer(
		const void*  data,
		uint32_t     sizeInBytes,
		uint32_t     strideInBytes,
		EnBufferKind kind
	)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		return m_buffers.Create(*m_device.Get(), data, sizeInBytes, strideInBytes, kind);
	}

	BufferHandle GraphicsDevice::CreateDynamicBuffer(
		uint32_t     capacityInBytes,
		uint32_t     strideInBytes,
		EnBufferKind kind
	)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		return m_buffers.CreateDynamic(*m_device.Get(), capacityInBytes, strideInBytes, kind);
	}

	void GraphicsDevice::UpdateBuffer(BufferHandle handle, const void* data, uint32_t sizeInBytes)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		m_buffers.Update(handle, data, sizeInBytes);
	}

	void GraphicsDevice::DestroyBuffer(BufferHandle handle)
	{
		m_buffers.Destroy(handle);
	}

	TextureHandle GraphicsDevice::CreateTexture2D(const void* pixels, uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		ID3D12Device&       device       = *m_device.Get();
		ID3D12CommandQueue& commandQueue = *m_commandQueue.Get();

		return m_textures.Create(device, commandQueue, m_fence, m_shaderVisibleHeap, pixels, width, height);
	}

	void GraphicsDevice::DestroyTexture(TextureHandle handle)
	{
		m_textures.Destroy(handle);
	}

	void GraphicsDevice::Resize(uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");
		FANG_ASSERT(!m_isFrameOpen, "フレームの途中でリサイズしようとしている");

		if (width == 0 || height == 0 || (m_swapChain.GetWidth() == width && m_swapChain.GetHeight() == height))
		{
			return;
		}

		// バックバッファを GPU が参照していない状態にしてからでないと ResizeBuffers が失敗する。
		m_fence.WaitForGPU(*m_commandQueue.Get());

		m_swapChain.Resize(*m_device.Get(), width, height);
	}

	CommandList* GraphicsDevice::BeginFrame(const ClearColor& clearColor)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");
		FANG_ASSERT(!m_isFrameOpen, "BeginFrame が二重に呼ばれている");

		// 前にこのバックバッファ用へ記録したコマンドのメモリを巻き戻して再利用する。
		// EndFrame で毎フレーム GPU の完了を待っているので、GPU が使用中のメモリを巻き戻す事故は起きない。
		// Reset が失敗するのは主にデバイスロストで、呼び出し側は nullptr を見てフレームループを畳む。
		ID3D12CommandAllocator* allocator = m_commandAllocators[m_swapChain.GetFrameIndex()].Get();
		if (!CheckHresult(allocator->Reset(), "コマンドアロケータの Reset"))
		{
			return nullptr;
		}

		// 記録口を「記録開始」状態に戻す。
		if (!CheckHresult(m_commandList->Reset(allocator, nullptr), "コマンドリストの Reset"))
		{
			return nullptr;
		}

		// このフレームでシェーダから見えるディスクリプタの置き場を宣言する。
		ID3D12DescriptorHeap* heaps[] = { m_shaderVisibleHeap.GetNative() };
		m_commandList->SetDescriptorHeaps(FANG_COUNT_OF(heaps), heaps);

		// リソースバリア: このバックバッファを「表示用」から「描き込み先」へ切り替える宣言。
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;					  // 「用途の遷移」の宣言。
		barrier.Transition.pResource   = m_swapChain.GetCurrentBackBuffer();      // 今回のバックバッファを指す
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;			  // これまでの用途は「表示用」
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;	  // 今回の用途は「描き込み先」
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; // 全てのミップマップを対象にする
		m_commandList->ResourceBarrier(1, &barrier);                              // 1 個のバリアを積む

		// 今回のバックバッファを描画先に据えて、背景色で塗りつぶす。
		const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_swapChain.GetCurrentRenderTargetView();
		m_commandList->OMSetRenderTargets(1, &renderTargetView, FALSE, nullptr);

		// クリア色を RGBA の float 配列に変換して渡す。D3D12 は 0.0〜1.0 の範囲で読む。
		const float clearValues[4] = { clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha };
		m_commandList->ClearRenderTargetView(renderTargetView, clearValues, 0, nullptr);

		// 公開型の記録口に生のコマンドリストを差して貸し出す。EndFrame で回収する。
		m_commandListWrapper.m_nativeCommandList = m_commandList.Get();

		m_isFrameOpen = true;

		return &m_commandListWrapper;
	}

	void GraphicsDevice::EndFrame()
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		if (!m_isFrameOpen)
		{
			return;
		}

		// BeginFrame の逆向きバリア。「描き込み先」から「表示用」へ戻してから Present する。
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = m_swapChain.GetCurrentBackBuffer();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_commandList->ResourceBarrier(1, &barrier);

		FANG_VERIFY(SUCCEEDED(m_commandList->Close()));

		ID3D12CommandList* commandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(FANG_COUNT_OF(commandLists), commandLists);

		m_swapChain.Present();

		// TODO: GPU を 2〜3 フレーム in-flight にする（Phase 3）。今は毎フレーム待つ。
		m_fence.WaitForGPU(*m_commandQueue.Get());
		m_commandListWrapper.m_nativeCommandList = nullptr;

		m_swapChain.UpdateFrameIndex();
		m_isFrameOpen = false;
	}
} // namespace fang::rhi
