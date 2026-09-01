/**
 * @file GraphicsDevice.cpp
 * @brief DirectX 12 のデバイス・スワップチェーン・コマンド記録の実装。
 */
#include "Pch.h"
#include "Core/Log/Assert.h"
#include "Core/Memory/Allocator.h"
#include "GraphicsDeviceImpl.h"

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
		FANG_ASSERT(m_impl == nullptr, "GraphicsDevice を二重に初期化している");
		FANG_ASSERT(desc.windowHandle != nullptr, "ウィンドウのハンドルが無い");

		m_impl = New<Impl>(HeapAllocator::GetInstance());
		if (m_impl == nullptr)
		{
			return false;
		}

		Impl& impl = *m_impl;

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

		if (!CheckHresult(::CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&impl.factory)), "DXGI ファクトリの生成"))
		{
			return false;
		}

		ComPtr<IDXGIAdapter1> adapter;
		for (UINT adapterIndex = 0;
			 impl.factory->EnumAdapterByGpuPreference(adapterIndex,
													  DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
													  IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
			 ++adapterIndex)
		{
			DXGI_ADAPTER_DESC1 adapterDesc{};
			FANG_VERIFY(SUCCEEDED(adapter->GetDesc1(&adapterDesc)));
			if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			{
				adapter.Reset();
				continue;
			}

			if (SUCCEEDED(::D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&impl.device))))
			{
				break;
			}

			adapter.Reset();
		}

		if (impl.device == nullptr)
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
				impl.device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels))))
		{
			FANG_LOG_INFO(RHI,
						  "D3D12 デバイスを作った (Feature Level {})",
						  ToDisplayName(featureLevels.MaxSupportedFeatureLevel));
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		if (!CheckHresult(impl.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&impl.commandQueue)),
						  "コマンドキューの生成"))
		{
			return false;
		}

		if (!impl.swapChain.Initialize(*impl.factory.Get(),
									   *impl.device.Get(),
									   *impl.commandQueue.Get(),
									   desc.windowHandle,
									   desc.width,
									   desc.height))
		{
			return false;
		}

		if (!impl.shaderVisibleHeap.Initialize(*impl.device.Get()))
		{
			return false;
		}

		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			if (!CheckHresult(impl.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
																  IID_PPV_ARGS(&impl.commandAllocators[bufferIndex])),
							  "コマンドアロケータの生成"))
			{
				return false;
			}
		}

		if (!CheckHresult(impl.device->CreateCommandList(0,
														 D3D12_COMMAND_LIST_TYPE_DIRECT,
														 impl.commandAllocators[impl.swapChain.GetFrameIndex()].Get(),
														 nullptr,
														 IID_PPV_ARGS(&impl.commandList)),
						  "コマンドリストの生成"))
		{
			return false;
		}

		FANG_VERIFY(SUCCEEDED(impl.commandList->Close()));

		if (!impl.fence.Initialize(*impl.device.Get()))
		{
			return false;
		}

		impl.commandListWrapper.m_device = this;

		return true;
	}

	void GraphicsDevice::Shutdown()
	{
		if (m_impl == nullptr)
		{
			return;
		}

		// 初期化が途中で失敗しているとキューが無い。積んだ仕事も無いので待たずに片付ける。
		if (m_impl->commandQueue != nullptr)
		{
			m_impl->fence.WaitForGPU(*m_impl->commandQueue.Get());
		}

		m_impl->fence.Shutdown();

		Delete(HeapAllocator::GetInstance(), m_impl);
		m_impl = nullptr;
	}

	PipelineHandle GraphicsDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		return m_impl->pipelines.Create(*m_impl->device.Get(), desc);
	}

	void GraphicsDevice::DestroyPipeline(PipelineHandle handle)
	{
		if (m_impl == nullptr)
		{
			return;
		}

		m_impl->pipelines.Destroy(handle);
	}

	BufferHandle GraphicsDevice::CreateBuffer(const void*  data,
											  uint32_t     sizeInBytes,
											  uint32_t     strideInBytes,
											  EnBufferKind kind)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		return m_impl->buffers.Create(*m_impl->device.Get(), data, sizeInBytes, strideInBytes, kind);
	}

	BufferHandle GraphicsDevice::CreateDynamicBuffer(uint32_t     capacityInBytes,
													 uint32_t     strideInBytes,
													 EnBufferKind kind)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		return m_impl->buffers.CreateDynamic(*m_impl->device.Get(), capacityInBytes, strideInBytes, kind);
	}

	void GraphicsDevice::UpdateBuffer(BufferHandle handle, const void* data, uint32_t sizeInBytes)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		m_impl->buffers.Update(handle, data, sizeInBytes);
	}

	void GraphicsDevice::DestroyBuffer(BufferHandle handle)
	{
		if (m_impl == nullptr)
		{
			return;
		}

		m_impl->buffers.Destroy(handle);
	}

	TextureHandle GraphicsDevice::CreateTexture2D(const void* pixels, uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;

		return impl.textures.Create(*impl.device.Get(),
									*impl.commandQueue.Get(),
									impl.fence,
									impl.shaderVisibleHeap,
									pixels,
									width,
									height);
	}

	void GraphicsDevice::DestroyTexture(TextureHandle handle)
	{
		if (m_impl == nullptr)
		{
			return;
		}

		m_impl->textures.Destroy(handle);
	}

	void GraphicsDevice::Resize(uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;
		FANG_ASSERT(!impl.isFrameOpen, "フレームの途中でリサイズしようとしている");

		if (width == 0 || height == 0 || (impl.swapChain.GetWidth() == width && impl.swapChain.GetHeight() == height))
		{
			return;
		}

		// バックバッファを GPU が参照していない状態にしてからでないと ResizeBuffers が失敗する。
		impl.fence.WaitForGPU(*impl.commandQueue.Get());

		impl.swapChain.Resize(*impl.device.Get(), width, height);
	}

	CommandList* GraphicsDevice::BeginFrame(const ClearColor& clearColor)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;
		FANG_ASSERT(!impl.isFrameOpen, "BeginFrame が二重に呼ばれている");

		ID3D12CommandAllocator* allocator = impl.commandAllocators[impl.swapChain.GetFrameIndex()].Get();
		if (!CheckHresult(allocator->Reset(), "コマンドアロケータの Reset"))
		{
			return nullptr;
		}

		if (!CheckHresult(impl.commandList->Reset(allocator, nullptr), "コマンドリストの Reset"))
		{
			return nullptr;
		}

		ID3D12DescriptorHeap* heaps[] = { impl.shaderVisibleHeap.GetNative() };
		impl.commandList->SetDescriptorHeaps(FANG_COUNT_OF(heaps), heaps);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = impl.swapChain.GetCurrentBackBuffer();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		impl.commandList->ResourceBarrier(1, &barrier);

		const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = impl.swapChain.GetCurrentRenderTargetView();
		impl.commandList->OMSetRenderTargets(1, &renderTargetView, FALSE, nullptr);

		const float clearValues[4] = { clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha };
		impl.commandList->ClearRenderTargetView(renderTargetView, clearValues, 0, nullptr);

		impl.commandListWrapper.m_nativeCommandList = impl.commandList.Get();

		impl.isFrameOpen = true;

		return &impl.commandListWrapper;
	}

	void GraphicsDevice::EndFrame()
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;
		if (!impl.isFrameOpen)
		{
			return;
		}

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = impl.swapChain.GetCurrentBackBuffer();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		impl.commandList->ResourceBarrier(1, &barrier);

		FANG_VERIFY(SUCCEEDED(impl.commandList->Close()));

		ID3D12CommandList* commandLists[] = { impl.commandList.Get() };
		impl.commandQueue->ExecuteCommandLists(FANG_COUNT_OF(commandLists), commandLists);

		impl.swapChain.Present();

		// TODO: GPU を 2〜3 フレーム in-flight にする（Phase 3）。今は毎フレーム待つ。
		impl.fence.WaitForGPU(*impl.commandQueue.Get());
		impl.commandListWrapper.m_nativeCommandList = nullptr;

		impl.swapChain.UpdateFrameIndex();
		impl.isFrameOpen = false;
	}
} // namespace fang::rhi
