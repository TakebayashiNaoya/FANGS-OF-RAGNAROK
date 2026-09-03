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

		//------------------------------------------------------------------------
		// 1. デバッグレイヤー
		// 　D3D12 の検証機能。有効にすると以降の全 API 呼び出しが検査され、
		// 　誤用(状態不一致・バリア漏れ・無効な引数)がメッセージで出る。
		// 　検査の分だけ遅くなるので開発ビルドのみ。
		//------------------------------------------------------------------------
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

		//------------------------------------------------------------------------
		// 2. DXGI ファクトリと D3D12 デバイス
		// 　DXGI は「GPU の列挙」と「描いた絵を画面に出す仕組み」を担当する層(D3D10〜12 共通)。
		// 　ファクトリはアダプタ列挙とスワップチェーン生成の入口。
		//------------------------------------------------------------------------
		if (!CheckHresult(::CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "DXGI ファクトリの生成"))
		{
			return false;
		}

		//------------------------------------------------------------------------
		// 3. アダプタのループ
		// 　アダプタ = GPU 1 基に対応するオブジェクト。
		// 　性能の高い順に列挙し、ソフトウェアラスタライザ(GPU なし環境用の CPU 描画。遅い)を除外し、
		// 　D3D12 デバイスを作れた最初の 1 基を採用する。
		// 　デバイスはこのプロセスと GPU をつなぐ API オブジェクトで、以降の全リソースの生成関数を持つ。
		//------------------------------------------------------------------------
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

		//------------------------------------------------------------------------
		// 4.Feature Level のログ
		// 　その GPU で何世代の機能が使えるかを起動ログに残すだけ。
		// 　実機で「起動してすぐ落ちた」ときの切り分け用。
		//------------------------------------------------------------------------
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

		//------------------------------------------------------------------------
		// 5. コマンドキュー
		// 　GPU に仕事を積むための列。CPU が「これやって」と積んで、GPU が自分のペースで消化する。
		//------------------------------------------------------------------------
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

		//-----------------------------------------------------------------------
		// 6. スワップチェーン
		// 　画面に出す絵の 2 枚組。表の 1 枚を見せている間に裏の 1 枚へ描き、Present で入れ替える。
		// 　裏の 1 枚が「バックバッファ」。
		//-----------------------------------------------------------------------
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

		//-----------------------------------------------------------------------
		// 7. 深度バッファ
		// 　ピクセルごとに最前面の奥行きを保持するテクスチャ。
		// 　描画時に比較して奥なら棄却する(深度テスト)。
		// 　これで描画順によらず前後関係が正しくなる。
		//-----------------------------------------------------------------------
		// 深度は画面と同じ大きさで持つ。以降は Resize でスワップチェーンと一緒に作り直す。
		if (!m_depthBuffer.Initialize(*m_device.Get(), desc.width, desc.height))
		{
			return false;
		}

		//-----------------------------------------------------------------------
		// 8. ディスクリプタヒープ
		// 　ディスクリプタ(リソースのアドレス・形式・大きさを書いた固定長の記述)の配列。
		// 　シェーダはリソースを生ポインタでなくこの配列の要素経由で参照する。
		// 　ここに置くのはシェーダから読むテクスチャの記述(SRV)。
		//-----------------------------------------------------------------------
		if (!m_shaderVisibleHeap.Initialize(*m_device.Get()))
		{
			return false;
		}

		//------------------------------------------------------------------------
		// 9. コマンドアロケータ 8×2 とコマンドリスト
		// 　リストはコマンドを記録する口で、記録されたコマンドの実体メモリはアロケータが持つ。
		// 　リストの Reset は書き込み先アロケータの付け替えだけで軽い。
		// 　アロケータの Reset はメモリの一括巻き戻しで、GPU がそこを実行し終えてからでないと呼べない
		// 　➡ バックバッファの面ごとに束を分けておく。
		//------------------------------------------------------------------------
		// 記録の口は起動時に全部そろえておく。フレームの途中で生成すると、そこだけ数ミリ秒の山ができる。
		for (uint32_t listIndex = 0; listIndex < MAX_COMMAND_LIST_COUNT; ++listIndex)
		{
			for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
			{
				if (!CheckHresult(
						m_device->CreateCommandAllocator(
							D3D12_COMMAND_LIST_TYPE_DIRECT,
							IID_PPV_ARGS(&m_commandAllocators[listIndex][bufferIndex])
						),
						"コマンドアロケータの生成"
					))
				{
					return false;
				}
			}
			// アロケータを設定する必要があるので、リストの生成はアロケータを作った後にする。
			if (!CheckHresult(
					m_device->CreateCommandList(
						0,
						D3D12_COMMAND_LIST_TYPE_DIRECT,
						m_commandAllocators[listIndex][m_swapChain.GetFrameIndex()].Get(),
						nullptr,
						IID_PPV_ARGS(&m_commandLists[listIndex])
					),
					"コマンドリストの生成"
				))
			{
				return false;
			}

			// 生成直後は記録中なので閉じておく。貸し出しは必ず Reset から始まる。
			FANG_VERIFY(SUCCEEDED(m_commandLists[listIndex]->Close()));

			m_commandListWrappers[listIndex].m_device = this;
		}

		//------------------------------------------------------------------------
		// 10. フェンス
		// 　CPU と GPU の両方から見える 64 bit カウンタ。
		// 　キューに「ここまでの仕事が終わったらカウンタを N にせよ」と積み、CPU 側は N になるまで待つ。
		// 　CPU-GPU 間の同期手段はこれだけ。
		//------------------------------------------------------------------------
		if (!m_fence.Initialize(*m_device.Get()))
		{
			return false;
		}

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
		for (uint32_t listIndex = 0; listIndex < MAX_COMMAND_LIST_COUNT; ++listIndex)
		{
			m_commandListWrappers[listIndex] = {};
			m_commandLists[listIndex].Reset();

			for (uint32_t bufferIndex = 0; bufferIndex < BACK_BUFFER_COUNT; ++bufferIndex)
			{
				m_commandAllocators[listIndex][bufferIndex].Reset();
			}
		}

		m_textures.Shutdown();
		m_buffers.Shutdown();
		m_pipelines.Shutdown();

		m_fence.Shutdown();
		m_shaderVisibleHeap.Shutdown();
		m_depthBuffer.Shutdown();
		m_swapChain.Shutdown();

		m_commandQueue.Reset();
		m_device.Reset();
		m_factory.Reset();

		m_acquiredCommandListCount = 0;

		m_isFrameOpen       = false;
		m_isFrameRecordable = false;
		m_isInitialized     = false;
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


	TextureHandle GraphicsDevice::CreateTexture2D(const TextureSource& source)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		ID3D12Device&       device       = *m_device.Get();
		ID3D12CommandQueue& commandQueue = *m_commandQueue.Get();

		return m_textures.Create(device, commandQueue, m_fence, m_shaderVisibleHeap, source);
	}


	TextureHandle GraphicsDevice::CreateTexture2D(const void* pixels, uint32_t width, uint32_t height)
	{
		const TextureMipLevel mipLevel{
			.pixels      = pixels,
			.width       = width,
			.height      = height,
			.rowPitch    = width * 4,
			.sizeInBytes = width * 4 * height,
		};

		const TextureSource source{
			.mipLevels = std::span<const TextureMipLevel>(&mipLevel, 1),
			.format    = EnTextureFormat::RGBA8,
		};

		return CreateTexture2D(source);
	}


	TextureHandle GraphicsDevice::CreateDepthTexture(uint32_t width, uint32_t height)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		return m_textures.CreateDepth(*m_device.Get(), m_shaderVisibleHeap, width, height);
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
		m_depthBuffer.Resize(*m_device.Get(), width, height);
	}


	void GraphicsDevice::LogDeviceRemovedReason() const
	{
		if (m_device == nullptr)
		{
			return;
		}

		// S_OK ならデバイスは生きていて、失敗の原因は別にある。それも分かるので常に出す。
		const HRESULT reason = m_device->GetDeviceRemovedReason();
		FANG_LOG_ERROR(RHI, "デバイス削除の理由: {:#010x}", static_cast<uint32_t>(reason));
	}


	void GraphicsDevice::BeginFrame()
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");
		FANG_ASSERT(!m_isFrameOpen, "BeginFrame が二重に呼ばれている");

		// ① フレームを開けたという帳簿付け。
		// 　 バリアもクリアも RT 設定もしない(どのパスが何に書くかは RenderGraph が決める)。
		m_isFrameOpen              = true;
		m_isFrameRecordable        = true;
		m_acquiredCommandListCount = 0;

		// ② 今の面のアロケータ 8 本を巻き戻し、記録済みコマンドのメモリを再利用する。
		// 　 EndFrame で毎フレーム GPU の完了を待っているので、GPU が使用中のメモリを巻き戻す事故は起きない。
		// 　 Reset が失敗するのは主にデバイスロストで、そのときは AcquireCommandList が貸さなくなる。
		const uint32_t frameIndex = m_swapChain.GetFrameIndex();
		for (uint32_t listIndex = 0; listIndex < MAX_COMMAND_LIST_COUNT; ++listIndex)
		{
			if (!CheckHresult(m_commandAllocators[listIndex][frameIndex]->Reset(), "コマンドアロケータの Reset"))
			{
				LogDeviceRemovedReason();
				m_isFrameRecordable = false;
				return;
			}
		}
	}


	CommandList* GraphicsDevice::AcquireCommandList()
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");
		FANG_ASSERT(m_isFrameOpen, "BeginFrame の外でコマンドリストを借りようとしている");
		FANG_ASSERT(
			m_acquiredCommandListCount < MAX_COMMAND_LIST_COUNT,
			"1 フレームで貸せるコマンドリストを使い切った"
		);

		if (!m_isFrameRecordable || m_acquiredCommandListCount >= MAX_COMMAND_LIST_COUNT)
		{
			return nullptr;
		}

		const uint32_t             listIndex   = m_acquiredCommandListCount;
		ID3D12CommandAllocator*    allocator   = m_commandAllocators[listIndex][m_swapChain.GetFrameIndex()].Get();
		ID3D12GraphicsCommandList* commandList = m_commandLists[listIndex].Get();

		// ① Reset で今の面のアロケータに付け替え、記録開始状態にする。
		if (!CheckHresult(commandList->Reset(allocator, nullptr), "コマンドリストの Reset"))
		{
			LogDeviceRemovedReason();
			m_isFrameRecordable = false;
			return nullptr;
		}

		// ② ディスクリプタヒープをこのリストに宣言する。リストをまたいで引き継がれないため。
		ID3D12DescriptorHeap* heaps[] = { m_shaderVisibleHeap.GetNative() };
		commandList->SetDescriptorHeaps(FANG_COUNT_OF(heaps), heaps);

		// ③ 生のコマンドリストを公開型 CommandList に包んで貸し出す(上位に d3d12.h を見せない)。
		// 　 EndFrame で回収する。
		CommandList& wrapper        = m_commandListWrappers[listIndex];
		wrapper.m_nativeCommandList = commandList;

		++m_acquiredCommandListCount;

		return &wrapper;
	}


	void GraphicsDevice::EndFrame(std::span<CommandList* const> commandLists)
	{
		FANG_ASSERT(m_isInitialized, "GraphicsDevice が初期化されていない");

		if (!m_isFrameOpen)
		{
			return;
		}

		// ① 貸したリストを未使用の分も全部 Close する。
		// 　 記録中のまま次のフレームに持ち越すと、Reset がエラーになるため。
		for (uint32_t listIndex = 0; listIndex < m_acquiredCommandListCount; ++listIndex)
		{
			FANG_VERIFY(SUCCEEDED(m_commandLists[listIndex]->Close()));
		}

		// ② 渡された順(= 実行順)に生のリストを集める。
		ID3D12CommandList* nativeCommandLists[MAX_COMMAND_LIST_COUNT]{};
		uint32_t           nativeCommandListCount = 0;
		for (CommandList* commandList : commandLists)
		{
			FANG_ASSERT(commandList != nullptr, "EndFrame に空のコマンドリストが混ざっている");
			FANG_ASSERT(
				nativeCommandListCount < MAX_COMMAND_LIST_COUNT,
				"貸した本数より多くのコマンドリストを渡している"
			);

			if (commandList == nullptr || nativeCommandListCount >= MAX_COMMAND_LIST_COUNT)
			{
				continue;
			}

			auto* nativeCommandList = static_cast<ID3D12GraphicsCommandList*>(commandList->m_nativeCommandList);
			FANG_ASSERT(nativeCommandList != nullptr, "このフレームに借りていないコマンドリストを渡している");
			if (nativeCommandList == nullptr)
			{
				continue;
			}

			nativeCommandLists[nativeCommandListCount] = nativeCommandList;
			++nativeCommandListCount;
		}

		// ③ ExecuteCommandLists で一括投入する。
		// 　 1 本も無ければ Present だけ行う。積むものが無いフレームでも画面の更新は止めない。
		if (nativeCommandListCount > 0)
		{
			m_commandQueue->ExecuteCommandLists(nativeCommandListCount, nativeCommandLists);
		}

		// ④ Present で表裏を入れ替える。
		m_swapChain.Present();

		// ⑤ フェンスで GPU の完了を待つ。
		// TODO: GPU を 2〜3 フレーム in-flight にする。今は毎フレーム待つ。
		m_fence.WaitForGPU(*m_commandQueue.Get());

		// ⑥ 貸し出しの帳簿を締め、次のフレームの面へ進める。
		for (uint32_t listIndex = 0; listIndex < m_acquiredCommandListCount; ++listIndex)
		{
			m_commandListWrappers[listIndex].m_nativeCommandList = nullptr;
		}

		m_acquiredCommandListCount = 0;

		m_swapChain.UpdateFrameIndex();

		m_isFrameOpen       = false;
		m_isFrameRecordable = false;
	}
} // namespace fang::rhi
