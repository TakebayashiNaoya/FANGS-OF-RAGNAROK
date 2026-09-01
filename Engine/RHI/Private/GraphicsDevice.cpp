/**
 * @file GraphicsDevice.cpp
 * @brief DirectX 12 のデバイス・スワップチェーン・コマンド記録の実装。
 */
#include "Pch.h"
#include "RHI/GraphicsDevice.h"
#include "Core/Log/Assert.h"
#include "Core/Memory/Allocator.h"
#include "D3D12Common.h"
#include <cstring>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

FANG_DEFINE_LOG_CATEGORY(RHI);


namespace fang::rhi
{
	namespace
	{
		constexpr uint32_t BACK_BUFFER_COUNT = 2;

		/** @brief シェーダから見えるディスクリプタヒープの大きさ。Phase 3 でリングバッファにする。 */
		constexpr uint32_t SHADER_VISIBLE_DESCRIPTOR_COUNT = 64;

		DXGI_FORMAT ToDxgiFormat(EnVertexFormat format)
		{
			switch (format)
			{
				case EnVertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
				case EnVertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
				case EnVertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
				case EnVertexFormat::UByte4Normalized: return DXGI_FORMAT_R8G8B8A8_UNORM;
			}

			return DXGI_FORMAT_UNKNOWN;
		}

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

	/**
	 * @brief GraphicsDevice の中身。
	 * @details Public のヘッダから d3d12.h を追い出すためだけの入れ物。
	 *          Pimpl イディオムの実装側で、ヘッダには前方宣言とポインタしかない。
	 *          狙いと代償は GraphicsDevice.h の m_impl のコメントにまとめてある。
	 */
	class GraphicsDevice::Impl
	{
	public:
		struct PipelineEntry
		{
			ComPtr<ID3D12RootSignature> rootSignature;  /**< シェーダに渡す資源の口（ルート定数・テクスチャ）の並び。 */
			ComPtr<ID3D12PipelineState> pipelineState;  /**< シェーダとステート一式を焼き固めたもの。 */
			uint32_t                    generation = 0; /**< ハンドルの世代と突き合わせる。 */
			bool                        isAlive    = false; /**< false なら空きスロット。次の生成で再利用される。 */
		};

		struct BufferEntry
		{
			ComPtr<ID3D12Resource>   resource;           /**< バッファの実体。 */
			D3D12_VERTEX_BUFFER_VIEW vertexBufferView{}; /**< Vertex のときに使う。GPU アドレス・大きさ・ストライド。 */
			D3D12_INDEX_BUFFER_VIEW  indexBufferView{};  /**< Index のときに使う。 */
			uint8_t*                 mappedPointer   = nullptr; /**< Map したまま持つ。動的バッファ以外は nullptr。 */
			uint32_t                 capacityInBytes = 0;       /**< 確保した大きさ。書き込みが超えないか確かめる。 */
			EnBufferKind             kind            = EnBufferKind::Vertex; /**< どちらのビューが有効かを決める。 */
			uint32_t                 generation      = 0;                    /**< ハンドルの世代と突き合わせる。 */
			bool                     isAlive         = false; /**< false なら空きスロット。次の生成で再利用される。 */
		};

		struct TextureEntry
		{
			ComPtr<ID3D12Resource> resource;                /**< テクスチャの実体。 */
			uint32_t               descriptorIndex = 0;     /**< シェーダ可視ヒープ上の位置。 */
			uint32_t               generation      = 0;     /**< ハンドルの世代と突き合わせる。 */
			bool                   isAlive         = false; /**< false なら空きスロット。次の生成で再利用される。 */
		};

		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView() const
		{
			D3D12_CPU_DESCRIPTOR_HANDLE handle = renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
			handle.ptr += static_cast<SIZE_T>(frameIndex) * renderTargetViewSize;
			return handle;
		}

		/**
		 * @brief 直前に積んだ分を GPU が消化するまで待つ。
		 * @details キューに積んだコマンドは積んだ瞬間には実行されておらず、GPU が自分のペースで消化する（CPU と GPU は非同期）。
		 *          この関数を抜けた時点で「ここまでに積んだ仕事は GPU 上で完全に終わっている」ことが保証される。
		 *          ただし CPU と GPU の並走を完全に止めるので高価。
		 *          使いどころは Resize / Shutdown / EndFrame（Phase 1 の割り切り）に限る。
		 */
		void WaitForGpu()
		{
			// 初期化が途中で失敗した状態で Shutdown から呼ばれても落ちないための守り。
			if (commandQueue == nullptr || fence == nullptr)
			{
				return;
			}

			// フェンスの値はフレーム番号ごとに分けず、単調増加の 1 本にする。
			// バックバッファごとに別々の値を積むと、値が前後して「もう完了している」と誤判定する。
			const uint64_t valueToWait = nextFenceValue;

			// Signal は「ここまでの仕事を全部終えたら fence に valueToWait を書け」という
			// 注文をキューの末尾に積む。キューは先入れ先出しなので、
			// 「fence に値が書かれた ⇔ それより前の仕事が全部終わった」が成立する。
			if (!CheckHresult(commandQueue->Signal(fence.Get(), valueToWait), "フェンスの Signal"))
			{
				return;
			}

			++nextFenceValue;

			// まず現在値を覗くだけ（待たない）。もう届いていれば何もせず帰る。
			if (fence->GetCompletedValue() < valueToWait)
			{
				// まだなら「fence が valueToWait に達したらこのイベントを点灯して」と予約し、
				// スレッドを OS に預けて眠る。ビジーループで CPU を焼かないための作法。
				FANG_VERIFY(SUCCEEDED(fence->SetEventOnCompletion(valueToWait, fenceEvent)));
				::WaitForSingleObject(fenceEvent, INFINITE);
			}
		}

		ComPtr<IDXGIFactory6>             factory;              /**< アダプタ列挙とスワップチェーン生成の入口。 */
		ComPtr<ID3D12Device>              device;               /**< D3D12 の本体。全リソースの生成元。 */
		ComPtr<ID3D12CommandQueue>        commandQueue;         /**< コマンドを GPU に流す唯一の列。 */
		ComPtr<IDXGISwapChain3>           swapChain;            /**< バックバッファの束。Present で画面に出す。 */
		ComPtr<ID3D12DescriptorHeap>      renderTargetViewHeap; /**< バックバッファ用 RTV の置き場。 */
		ComPtr<ID3D12DescriptorHeap>      shaderVisibleHeap;    /**< シェーダから見える SRV の置き場。 */
		ComPtr<ID3D12Resource>            backBuffers[BACK_BUFFER_COUNT]; /**< 描画先。frameIndex が指す 1 枚に描く。 */
		ComPtr<ID3D12CommandAllocator>    commandAllocators[BACK_BUFFER_COUNT]; /**< コマンドの記録メモリ。 */
		ComPtr<ID3D12GraphicsCommandList> commandList; /**< コマンドの記録口。毎フレーム Reset する。 */
		ComPtr<ID3D12Fence>               fence;       /**< GPU の進み具合を知るカウンタ。WaitForGpu で使う。 */

		uint64_t nextFenceValue = 1;       /**< 次に Signal する値。単調増加させる。 */
		HANDLE   fenceEvent     = nullptr; /**< フェンスの完了を待つための OS のイベント。 */

		uint32_t renderTargetViewSize        = 0; /**< RTV 1 個分のバイト数。GPU ごとに違う。 */
		uint32_t shaderVisibleDescriptorSize = 0; /**< SRV 1 個分のバイト数。 */
		uint32_t nextShaderVisibleDescriptor = 0; /**< 次に使う SRV の位置。今は返却しないので増える一方。 */

		uint32_t frameIndex  = 0;     /**< 今描いているバックバッファの番号。 */
		uint32_t width       = 0;     /**< バックバッファの幅（ピクセル）。 */
		uint32_t height      = 0;     /**< バックバッファの高さ（ピクセル）。 */
		bool     isFrameOpen = false; /**< BeginFrame と EndFrame の間なら true。 */

		// TODO: Core の Array<T> とプールができたら差し替える。
		std::vector<PipelineEntry> pipelines; /**< PipelineHandle.index で引く台帳。 */
		std::vector<BufferEntry>   buffers;   /**< BufferHandle.index で引く台帳。 */
		std::vector<TextureEntry>  textures;  /**< TextureHandle.index で引く台帳。 */

		CommandList commandListWrapper; /**< BeginFrame が返す公開型。中身は commandList を指す。 */
	};

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

		Impl& impl  = *m_impl;
		impl.width  = desc.width;
		impl.height = desc.height;

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

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.BufferCount      = BACK_BUFFER_COUNT;
		swapChainDesc.Width            = desc.width;
		swapChainDesc.Height           = desc.height;
		swapChainDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;

		ComPtr<IDXGISwapChain1> swapChain;
#if FANG_PLATFORM_WINDOWS
		const HRESULT swapChainResult = impl.factory->CreateSwapChainForHwnd(impl.commandQueue.Get(),
																			 static_cast<HWND>(desc.windowHandle),
																			 &swapChainDesc,
																			 nullptr,
																			 nullptr,
																			 &swapChain);
#else
		// UWP は CoreWindow を IUnknown* にして渡す。
		const HRESULT swapChainResult =
			impl.factory->CreateSwapChainForCoreWindow(impl.commandQueue.Get(),
													   static_cast<IUnknown*>(desc.windowHandle),
													   &swapChainDesc,
													   nullptr,
													   &swapChain);
#endif
		if (!CheckHresult(swapChainResult, "スワップチェーンの生成"))
		{
			return false;
		}

		if (!CheckHresult(swapChain.As(&impl.swapChain), "スワップチェーンの問い合わせ"))
		{
			return false;
		}

		impl.frameIndex = impl.swapChain->GetCurrentBackBufferIndex();

		D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewHeapDesc{};
		renderTargetViewHeapDesc.NumDescriptors = BACK_BUFFER_COUNT;
		renderTargetViewHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		if (!CheckHresult(
				impl.device->CreateDescriptorHeap(&renderTargetViewHeapDesc, IID_PPV_ARGS(&impl.renderTargetViewHeap)),
				"レンダーターゲットビューのヒープ生成"))
		{
			return false;
		}

		impl.renderTargetViewSize = impl.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_DESCRIPTOR_HEAP_DESC shaderVisibleHeapDesc{};
		shaderVisibleHeapDesc.NumDescriptors = SHADER_VISIBLE_DESCRIPTOR_COUNT;
		shaderVisibleHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		shaderVisibleHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (!CheckHresult(
				impl.device->CreateDescriptorHeap(&shaderVisibleHeapDesc, IID_PPV_ARGS(&impl.shaderVisibleHeap)),
				"シェーダ可視ディスクリプタヒープの生成"))
		{
			return false;
		}

		impl.shaderVisibleDescriptorSize =
			impl.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = impl.renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			if (!CheckHresult(impl.swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&impl.backBuffers[bufferIndex])),
							  "バックバッファの取得"))
			{
				return false;
			}

			impl.device->CreateRenderTargetView(impl.backBuffers[bufferIndex].Get(), nullptr, renderTargetView);
			renderTargetView.ptr += impl.renderTargetViewSize;

			if (!CheckHresult(impl.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
																  IID_PPV_ARGS(&impl.commandAllocators[bufferIndex])),
							  "コマンドアロケータの生成"))
			{
				return false;
			}
		}

		if (!CheckHresult(impl.device->CreateCommandList(0,
														 D3D12_COMMAND_LIST_TYPE_DIRECT,
														 impl.commandAllocators[impl.frameIndex].Get(),
														 nullptr,
														 IID_PPV_ARGS(&impl.commandList)),
						  "コマンドリストの生成"))
		{
			return false;
		}

		FANG_VERIFY(SUCCEEDED(impl.commandList->Close()));

		if (!CheckHresult(impl.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl.fence)),
						  "フェンスの生成"))
		{
			return false;
		}

		impl.fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (impl.fenceEvent == nullptr)
		{
			FANG_LOG_ERROR(RHI, "フェンス用のイベントを作れなかった");
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

		m_impl->WaitForGpu();

		if (m_impl->fenceEvent != nullptr)
		{
			::CloseHandle(m_impl->fenceEvent);
			m_impl->fenceEvent = nullptr;
		}

		Delete(HeapAllocator::GetInstance(), m_impl);
		m_impl = nullptr;
	}

	PipelineHandle GraphicsDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;

		D3D12_DESCRIPTOR_RANGE textureRange{};
		textureRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		textureRange.NumDescriptors     = 1;
		textureRange.BaseShaderRegister = 0;

		D3D12_ROOT_PARAMETER rootParameters[2]{};
		uint32_t             rootParameterCount = 0;

		if (desc.rootConstantCount > 0)
		{
			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_VERTEX;

			parameter.Constants.Num32BitValues = desc.rootConstantCount;
			parameter.Constants.ShaderRegister = 0;
			++rootParameterCount;
		}

		if (desc.hasTexture)
		{
			D3D12_ROOT_PARAMETER& parameter = rootParameters[rootParameterCount];
			parameter.ParameterType         = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.ShaderVisibility      = D3D12_SHADER_VISIBILITY_PIXEL;

			parameter.DescriptorTable.NumDescriptorRanges = 1;
			parameter.DescriptorTable.pDescriptorRanges   = &textureRange;
			++rootParameterCount;
		}

		// サンプラは 1 種類しか要らないので静的サンプラで済ませる。
		D3D12_STATIC_SAMPLER_DESC staticSampler{};
		staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
		staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.NumParameters     = rootParameterCount;
		rootSignatureDesc.pParameters       = rootParameterCount > 0 ? rootParameters : nullptr;
		rootSignatureDesc.NumStaticSamplers = desc.hasTexture ? 1u : 0u;
		rootSignatureDesc.pStaticSamplers   = desc.hasTexture ? &staticSampler : nullptr;
		rootSignatureDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> serializedRootSignature;
		ComPtr<ID3DBlob> errors;
		if (!CheckHresult(::D3D12SerializeRootSignature(&rootSignatureDesc,
														D3D_ROOT_SIGNATURE_VERSION_1,
														&serializedRootSignature,
														&errors),
						  "ルートシグネチャのシリアライズ"))
		{
			return PipelineHandle{};
		}

		Impl::PipelineEntry entry;
		if (!CheckHresult(impl.device->CreateRootSignature(0,
														   serializedRootSignature->GetBufferPointer(),
														   serializedRootSignature->GetBufferSize(),
														   IID_PPV_ARGS(&entry.rootSignature)),
						  "ルートシグネチャの生成"))
		{
			return PipelineHandle{};
		}

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		inputElements.reserve(desc.vertexLayout.size());
		for (const VertexAttribute& attribute : desc.vertexLayout)
		{
			D3D12_INPUT_ELEMENT_DESC element{};
			element.SemanticName      = attribute.semanticName;
			element.SemanticIndex     = attribute.semanticIndex;
			element.Format            = ToDxgiFormat(attribute.format);
			element.AlignedByteOffset = attribute.offsetInBytes;
			element.InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			inputElements.push_back(element);
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = entry.rootSignature.Get();
		pipelineDesc.VS             = { desc.vertexShaderBytecode.data(), desc.vertexShaderBytecode.size() };
		pipelineDesc.PS             = { desc.pixelShaderBytecode.data(), desc.pixelShaderBytecode.size() };

		pipelineDesc.InputLayout           = { inputElements.data(), static_cast<UINT>(inputElements.size()) };
		pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		pipelineDesc.NumRenderTargets = 1;
		pipelineDesc.RTVFormats[0]    = DXGI_FORMAT_R8G8B8A8_UNORM;
		pipelineDesc.SampleDesc.Count = 1;
		pipelineDesc.SampleMask       = UINT_MAX;

		pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		pipelineDesc.RasterizerState.CullMode = desc.isAlphaBlendEnabled ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
		pipelineDesc.RasterizerState.DepthClipEnable = TRUE;

		D3D12_RENDER_TARGET_BLEND_DESC& blend = pipelineDesc.BlendState.RenderTarget[0];
		blend.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
		if (desc.isAlphaBlendEnabled)
		{
			blend.BlendEnable    = TRUE;
			blend.SrcBlend       = D3D12_BLEND_SRC_ALPHA;
			blend.DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOp        = D3D12_BLEND_OP_ADD;
			blend.SrcBlendAlpha  = D3D12_BLEND_ONE;
			blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
		}

		if (!CheckHresult(impl.device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&entry.pipelineState)),
						  "パイプラインステートの生成"))
		{
			return PipelineHandle{};
		}

		entry.isAlive = true;

		for (uint32_t index = 0; index < static_cast<uint32_t>(impl.pipelines.size()); ++index)
		{
			if (!impl.pipelines[index].isAlive)
			{
				entry.generation      = impl.pipelines[index].generation + 1;
				impl.pipelines[index] = entry;
				return PipelineHandle{ index, entry.generation };
			}
		}

		impl.pipelines.push_back(entry);
		return PipelineHandle{ static_cast<uint32_t>(impl.pipelines.size() - 1), entry.generation };
	}

	void GraphicsDevice::DestroyPipeline(PipelineHandle handle)
	{
		if (m_impl == nullptr || !handle.IsValid() || handle.index >= m_impl->pipelines.size())
		{
			return;
		}

		Impl::PipelineEntry& entry = m_impl->pipelines[handle.index];
		if (entry.generation != handle.generation)
		{
			return;
		}

		entry.rootSignature.Reset();
		entry.pipelineState.Reset();
		entry.isAlive = false;
	}

	namespace
	{
		/** @brief アップロードヒープにバッファを作る。 */
		bool CreateUploadBuffer(ID3D12Device* device, uint32_t sizeInBytes, ComPtr<ID3D12Resource>& outResource)
		{
			D3D12_HEAP_PROPERTIES heapProperties{};
			heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC resourceDesc{};
			resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
			resourceDesc.Width            = sizeInBytes;
			resourceDesc.Height           = 1;
			resourceDesc.DepthOrArraySize = 1;
			resourceDesc.MipLevels        = 1;
			resourceDesc.Format           = DXGI_FORMAT_UNKNOWN;
			resourceDesc.SampleDesc.Count = 1;
			resourceDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			return CheckHresult(device->CreateCommittedResource(&heapProperties,
																D3D12_HEAP_FLAG_NONE,
																&resourceDesc,
																D3D12_RESOURCE_STATE_GENERIC_READ,
																nullptr,
																IID_PPV_ARGS(&outResource)),
								"アップロードバッファの生成");
		}
	} // namespace

	BufferHandle GraphicsDevice::CreateBuffer(const void*  data,
											  uint32_t     sizeInBytes,
											  uint32_t     strideInBytes,
											  EnBufferKind kind)
	{
		const BufferHandle handle = CreateDynamicBuffer(sizeInBytes, strideInBytes, kind);
		if (handle.IsValid())
		{
			UpdateBuffer(handle, data, sizeInBytes);
		}

		return handle;
	}

	BufferHandle GraphicsDevice::CreateDynamicBuffer(uint32_t     capacityInBytes,
													 uint32_t     strideInBytes,
													 EnBufferKind kind)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;

		// Phase 1 はアップロードヒープに置いたままにする。既定ヒープへの転送は Phase 3。
		Impl::BufferEntry entry;
		if (!CreateUploadBuffer(impl.device.Get(), capacityInBytes, entry.resource))
		{
			return BufferHandle{};
		}

		void*       mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		if (!CheckHresult(entry.resource->Map(0, &readRange, &mapped), "バッファの Map"))
		{
			return BufferHandle{};
		}

		entry.mappedPointer   = static_cast<uint8_t*>(mapped);
		entry.capacityInBytes = capacityInBytes;
		entry.kind            = kind;
		entry.isAlive         = true;

		if (kind == EnBufferKind::Vertex)
		{
			entry.vertexBufferView.BufferLocation = entry.resource->GetGPUVirtualAddress();
			entry.vertexBufferView.SizeInBytes    = capacityInBytes;
			entry.vertexBufferView.StrideInBytes  = strideInBytes;
		}
		else
		{
			entry.indexBufferView.BufferLocation = entry.resource->GetGPUVirtualAddress();
			entry.indexBufferView.SizeInBytes    = capacityInBytes;
			entry.indexBufferView.Format         = strideInBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(impl.buffers.size()); ++index)
		{
			if (!impl.buffers[index].isAlive)
			{
				entry.generation    = impl.buffers[index].generation + 1;
				impl.buffers[index] = entry;
				return BufferHandle{ index, entry.generation };
			}
		}

		impl.buffers.push_back(entry);
		return BufferHandle{ static_cast<uint32_t>(impl.buffers.size() - 1), entry.generation };
	}

	void GraphicsDevice::UpdateBuffer(BufferHandle handle, const void* data, uint32_t sizeInBytes)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");
		FANG_ASSERT(handle.IsValid() && handle.index < m_impl->buffers.size(), "無効なバッファハンドル");

		Impl::BufferEntry& entry = m_impl->buffers[handle.index];
		FANG_ASSERT(entry.isAlive && entry.generation == handle.generation, "解放済みのバッファハンドル");
		FANG_ASSERT(sizeInBytes <= entry.capacityInBytes, "バッファの容量を超えて書き込もうとしている");

		std::memcpy(entry.mappedPointer, data, sizeInBytes);
	}

	void GraphicsDevice::DestroyBuffer(BufferHandle handle)
	{
		if (m_impl == nullptr || !handle.IsValid() || handle.index >= m_impl->buffers.size())
		{
			return;
		}

		Impl::BufferEntry& entry = m_impl->buffers[handle.index];
		if (entry.generation != handle.generation)
		{
			return;
		}

		if (entry.mappedPointer != nullptr)
		{
			entry.resource->Unmap(0, nullptr);
			entry.mappedPointer = nullptr;
		}

		entry.resource.Reset();
		entry.isAlive = false;
	}

	TextureHandle GraphicsDevice::CreateTexture2D(const void* pixels, uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;
		if (impl.nextShaderVisibleDescriptor >= SHADER_VISIBLE_DESCRIPTOR_COUNT)
		{
			FANG_LOG_ERROR(RHI, "シェーダ可視ディスクリプタが足りない");
			return TextureHandle{};
		}

		D3D12_HEAP_PROPERTIES defaultHeapProperties{};
		defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width            = width;
		textureDesc.Height           = height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels        = 1;
		textureDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		Impl::TextureEntry entry;
		if (!CheckHresult(impl.device->CreateCommittedResource(&defaultHeapProperties,
															   D3D12_HEAP_FLAG_NONE,
															   &textureDesc,
															   D3D12_RESOURCE_STATE_COPY_DEST,
															   nullptr,
															   IID_PPV_ARGS(&entry.resource)),
						  "テクスチャの生成"))
		{
			return TextureHandle{};
		}

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT64                             uploadSize = 0;
		impl.device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

		ComPtr<ID3D12Resource> uploadBuffer;
		if (!CreateUploadBuffer(impl.device.Get(), static_cast<uint32_t>(uploadSize), uploadBuffer))
		{
			return TextureHandle{};
		}

		uint8_t*    mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		if (!CheckHresult(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
						  "テクスチャ転送用の Map"))
		{
			return TextureHandle{};
		}

		// 行ごとのピッチが 256 バイト境界に合わされるので 1 行ずつ詰める。
		const uint8_t* source = static_cast<const uint8_t*>(pixels);
		for (uint32_t row = 0; row < height; ++row)
		{
			std::memcpy(mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
						source + static_cast<size_t>(row) * width * 4,
						static_cast<size_t>(width) * 4);
		}

		uploadBuffer->Unmap(0, nullptr);

		// 転送はフレームの外で済ませたいので、その場で 1 本流して待つ。
		ComPtr<ID3D12CommandAllocator>    uploadAllocator;
		ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
		if (!CheckHresult(
				impl.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator)),
				"転送用コマンドアロケータの生成"))
		{
			return TextureHandle{};
		}

		if (!CheckHresult(impl.device->CreateCommandList(0,
														 D3D12_COMMAND_LIST_TYPE_DIRECT,
														 uploadAllocator.Get(),
														 nullptr,
														 IID_PPV_ARGS(&uploadCommandList)),
						  "転送用コマンドリストの生成"))
		{
			return TextureHandle{};
		}

		D3D12_TEXTURE_COPY_LOCATION copySource{};
		copySource.pResource       = uploadBuffer.Get();
		copySource.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copySource.PlacedFootprint = footprint;

		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource        = entry.resource.Get();
		destination.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = 0;

		uploadCommandList->CopyTextureRegion(&destination, 0, 0, 0, &copySource, nullptr);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = entry.resource.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		uploadCommandList->ResourceBarrier(1, &barrier);

		FANG_VERIFY(SUCCEEDED(uploadCommandList->Close()));

		ID3D12CommandList* commandLists[] = { uploadCommandList.Get() };
		impl.commandQueue->ExecuteCommandLists(FANG_COUNT_OF(commandLists), commandLists);
		impl.WaitForGpu();

		entry.descriptorIndex = impl.nextShaderVisibleDescriptor;
		++impl.nextShaderVisibleDescriptor;

		D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		viewDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.Texture2D.MipLevels     = 1;

		D3D12_CPU_DESCRIPTOR_HANDLE descriptor = impl.shaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
		descriptor.ptr += static_cast<SIZE_T>(entry.descriptorIndex) * impl.shaderVisibleDescriptorSize;
		impl.device->CreateShaderResourceView(entry.resource.Get(), &viewDesc, descriptor);

		entry.isAlive = true;

		for (uint32_t index = 0; index < static_cast<uint32_t>(impl.textures.size()); ++index)
		{
			if (!impl.textures[index].isAlive)
			{
				entry.generation     = impl.textures[index].generation + 1;
				impl.textures[index] = entry;
				return TextureHandle{ index, entry.generation };
			}
		}

		impl.textures.push_back(entry);
		return TextureHandle{ static_cast<uint32_t>(impl.textures.size() - 1), entry.generation };
	}

	void GraphicsDevice::DestroyTexture(TextureHandle handle)
	{
		if (m_impl == nullptr || !handle.IsValid() || handle.index >= m_impl->textures.size())
		{
			return;
		}

		Impl::TextureEntry& entry = m_impl->textures[handle.index];
		if (entry.generation != handle.generation)
		{
			return;
		}

		// TODO: ディスクリプタのスロットも返す（Phase 3 でリングバッファにするときに）。
		entry.resource.Reset();
		entry.isAlive = false;
	}

	void GraphicsDevice::Resize(uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;
		FANG_ASSERT(!impl.isFrameOpen, "フレームの途中でリサイズしようとしている");

		if (width == 0 || height == 0 || (impl.width == width && impl.height == height))
		{
			return;
		}

		// バックバッファを GPU が参照していない状態にしてからでないと ResizeBuffers が失敗する。
		impl.WaitForGpu();

		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			impl.backBuffers[bufferIndex].Reset();
		}

		if (!CheckHresult(
				impl.swapChain->ResizeBuffers(BACK_BUFFER_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
				"スワップチェーンのリサイズ"))
		{
			return;
		}

		impl.width      = width;
		impl.height     = height;
		impl.frameIndex = impl.swapChain->GetCurrentBackBufferIndex();

		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = impl.renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
		for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
		{
			if (!CheckHresult(impl.swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&impl.backBuffers[bufferIndex])),
							  "バックバッファの取得"))
			{
				return;
			}

			impl.device->CreateRenderTargetView(impl.backBuffers[bufferIndex].Get(), nullptr, renderTargetView);
			renderTargetView.ptr += impl.renderTargetViewSize;
		}

		FANG_LOG_INFO(RHI, "バックバッファを作り直した ({}x{})", width, height);
	}

	CommandList* GraphicsDevice::BeginFrame(const ClearColor& clearColor)
	{
		FANG_ASSERT(m_impl != nullptr, "GraphicsDevice が初期化されていない");

		Impl& impl = *m_impl;
		FANG_ASSERT(!impl.isFrameOpen, "BeginFrame が二重に呼ばれている");

		ID3D12CommandAllocator* allocator = impl.commandAllocators[impl.frameIndex].Get();
		if (!CheckHresult(allocator->Reset(), "コマンドアロケータの Reset"))
		{
			return nullptr;
		}

		if (!CheckHresult(impl.commandList->Reset(allocator, nullptr), "コマンドリストの Reset"))
		{
			return nullptr;
		}

		ID3D12DescriptorHeap* heaps[] = { impl.shaderVisibleHeap.Get() };
		impl.commandList->SetDescriptorHeaps(FANG_COUNT_OF(heaps), heaps);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = impl.backBuffers[impl.frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		impl.commandList->ResourceBarrier(1, &barrier);

		const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = impl.GetCurrentRenderTargetView();
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

		barrier.Transition.pResource   = impl.backBuffers[impl.frameIndex].Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		impl.commandList->ResourceBarrier(1, &barrier);

		FANG_VERIFY(SUCCEEDED(impl.commandList->Close()));

		ID3D12CommandList* commandLists[] = { impl.commandList.Get() };
		impl.commandQueue->ExecuteCommandLists(FANG_COUNT_OF(commandLists), commandLists);

		FANG_VERIFY(SUCCEEDED(impl.swapChain->Present(1, 0)));

		// TODO: GPU を 2〜3 フレーム in-flight にする（Phase 3）。今は毎フレーム待つ。
		impl.WaitForGpu();
		impl.commandListWrapper.m_nativeCommandList = nullptr;

		impl.frameIndex  = impl.swapChain->GetCurrentBackBufferIndex();
		impl.isFrameOpen = false;
	}

	/***************************************************************************************************/

	void CommandList::SetViewport(uint32_t width, uint32_t height)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		D3D12_VIEWPORT viewport{};
		viewport.Width    = static_cast<float>(width);
		viewport.Height   = static_cast<float>(height);
		viewport.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &viewport);

		SetScissor(0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height));
	}

	void CommandList::SetScissor(int32_t left, int32_t top, int32_t right, int32_t bottom)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		const D3D12_RECT scissorRect{ left, top, right, bottom };
		commandList->RSSetScissorRects(1, &scissorRect);
	}

	void CommandList::SetPipeline(PipelineHandle pipeline)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		GraphicsDevice::Impl& impl = *m_device->m_impl;
		FANG_ASSERT(pipeline.IsValid() && pipeline.index < impl.pipelines.size(), "無効なパイプラインハンドル");

		const GraphicsDevice::Impl::PipelineEntry& entry = impl.pipelines[pipeline.index];
		FANG_ASSERT(entry.isAlive && entry.generation == pipeline.generation, "解放済みのパイプラインハンドル");

		commandList->SetGraphicsRootSignature(entry.rootSignature.Get());
		commandList->SetPipelineState(entry.pipelineState.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	void CommandList::SetVertexBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		GraphicsDevice::Impl& impl = *m_device->m_impl;
		FANG_ASSERT(buffer.IsValid() && buffer.index < impl.buffers.size(), "無効な頂点バッファハンドル");

		const GraphicsDevice::Impl::BufferEntry& entry = impl.buffers[buffer.index];
		FANG_ASSERT(entry.isAlive && entry.generation == buffer.generation, "解放済みの頂点バッファハンドル");

		commandList->IASetVertexBuffers(0, 1, &entry.vertexBufferView);
	}

	void CommandList::SetIndexBuffer(BufferHandle buffer)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		GraphicsDevice::Impl& impl = *m_device->m_impl;
		FANG_ASSERT(buffer.IsValid() && buffer.index < impl.buffers.size(), "無効なインデックスバッファハンドル");

		const GraphicsDevice::Impl::BufferEntry& entry = impl.buffers[buffer.index];
		FANG_ASSERT(entry.isAlive && entry.generation == buffer.generation, "解放済みのインデックスバッファハンドル");

		commandList->IASetIndexBuffer(&entry.indexBufferView);
	}

	void CommandList::SetRootConstants(const void* values, uint32_t count32BitValues)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		commandList->SetGraphicsRoot32BitConstants(0, count32BitValues, values, 0);
	}

	void CommandList::SetTexture(TextureHandle texture)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		GraphicsDevice::Impl& impl = *m_device->m_impl;
		FANG_ASSERT(texture.IsValid() && texture.index < impl.textures.size(), "無効なテクスチャハンドル");

		const GraphicsDevice::Impl::TextureEntry& entry = impl.textures[texture.index];
		FANG_ASSERT(entry.isAlive && entry.generation == texture.generation, "解放済みのテクスチャハンドル");

		D3D12_GPU_DESCRIPTOR_HANDLE descriptor = impl.shaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
		descriptor.ptr += static_cast<UINT64>(entry.descriptorIndex) * impl.shaderVisibleDescriptorSize;

		// ルート定数がある構成ではテーブルは 2 番目に来る。
		commandList->SetGraphicsRootDescriptorTable(1, descriptor);
	}

	void CommandList::Draw(uint32_t vertexCount)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		commandList->DrawInstanced(vertexCount, 1, 0, 0);
	}

	void CommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
	{
		ID3D12GraphicsCommandList* commandList = static_cast<ID3D12GraphicsCommandList*>(m_nativeCommandList);
		FANG_ASSERT(commandList != nullptr, "フレームの外でコマンドを積んでいる");

		commandList->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
	}
} // namespace fang::rhi
