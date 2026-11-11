/**
 * @file RenderContext.h
 * @brief レンダーコンテキストクラス
 */
#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <vector>
#include "Core/Noncopyable.h"
#include "CpuProfiler.h"
#include "GpuProfiler.h"


namespace fangEngine::render
{
	/**
	 * @brief DeviceなどGPUを動かすためのオブジェクトを持ち、GPUに命令を渡して画面に出すクラス
	 * @details シーンとビューはRendererが持ち、このクラスはシーンとビューを知らない。
	 *          Rendererに入れると、シーンを足すときにGPUの部分をまた外へ出すことになるため層を分ける。
	 *          生のHANDLEを持っておりコピーすると同じイベントを2回閉じてしまうためコピー禁止。
	 */
	class RenderContext : private core::Noncopyable
	{
	public:
		static constexpr int         RENDER_TARGET_COUNT  = 2;
		static constexpr DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
		static constexpr DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;


	public:
		/**
		 * @brief GPUの処理が終わるのを待ってから破棄する
		 */
		~RenderContext();

		/**
		 * @brief 描画に使うGPUのオブジェクトを作る
		 * @param window スワップチェーンの描画先のウィンドウ
		 * @return 全部作れたらtrue、1つでも失敗したらfalse
		 */
		bool Initialize(HWND window);

		/**
		 * @brief 1フレームの描画開始処理を行う
		 * @return 成功したらtrue、失敗したらfalse
		 */
		bool BeginFrame();

		/**
		 * @brief 1フレームの描画終了処理を行う
		 * @return 成功したらtrue、失敗したらfalse
		 */
		bool EndFrame();

		/**
		 * @brief GPUの処理が完了するまで待機する
		 * @return GPUの処理が完了したらtrue、失敗したらfalse
		 */
		bool WaitForGpu();

		/**
		 * @brief アップロード処理を開始する
		 * @details 起動のときだけ呼ぶ。GPUが何もしていない前提で、Resetの前に待たないため。
		 * @return 成功したらtrue、失敗したらfalse
		 */
		bool BeginUpload();

		/**
		 * @brief アップロード処理を終了する
		 * @return 成功したらtrue、失敗したらfalse
		 */
		bool EndUpload();

		/**
		 * @brief アップロード用のバッファを保持する
		 * @param uploadBuffer アップロード用のバッファ
		 */
		inline void KeepUntilUploaded(Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer) { m_uploadBuffers.push_back(uploadBuffer); }

		/**
		 * @brief デバイスを取得する
		 * @return デバイスのポインタ
		 */
		inline ID3D12Device* GetDevice() const { return m_device.Get(); }

		/**
		 * @brief コマンドキューを取得する
		 * @return コマンドキューのポインタ
		 */
		inline ID3D12CommandQueue* GetCommandQueue() const { return m_commandQueue.Get(); }

		/**
		 * @brief コマンドリストを取得する
		 * @return コマンドリストのポインタ
		 */
		inline ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }

		/**
		 * @brief 直前のフレームのCPU処理の時間を取得する
		 * @return 待ちを除いたCPU処理の時間(ms)
		 */
		inline float GetCpuFrameMilliseconds() const { return m_cpuProfiler.GetFrameMilliseconds(); }

		/**
		 * @brief 2フレーム前のGPU処理の時間を取得する
		 * @return GPUの処理の時間(ms)
		 */
		inline float GetGpuFrameMilliseconds() const { return m_gpuProfiler.GetFrameMilliseconds(); }

		/**
		 * @brief 現在描画中のバッファのインデックスを取得する
		 * @details BeginFrameのあとで呼ぶ。前に呼ぶと、前のフレームのインデックスが返るため。
		 * @return バックバッファのインデックス
		 */
		inline UINT GetBackBufferIndex() const { return m_backBufferIndex; }


	private:
		/**
		 * @brief D3D12デバイスを作成する
		 * @return デバイスの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateDevice();

		/**
		 * @brief D3D12コマンドキューを作成する
		 * @return コマンドキューの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateCommandQueue();

		/**
		 * @brief DXGIスワップチェーンを作成する
		 * @param window スワップチェーンの描画先のウィンドウ
		 * @return スワップチェーンの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateSwapChain(HWND window);

		/**
		 * @brief レンダーターゲットビューを作成する
		 * @return レンダーターゲットビューの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateRenderTargetViews();

		/**
		 * @brief デプスステンシルビューを作成する
		 * @return デプスステンシルビューの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateDepthStencilView();

		/**
		 * @brief コマンドリストとアロケータを作成する
		 * @return コマンドリストの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateCommandAllocatorAndList();

		/**
		 * @brief フェンスを作成する
		 * @return フェンスの作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateFence();

		/**
		 * @brief 新しい番号を発行してQueueの最後に積む
		 * @return フェンスのシグナル送信に成功したらtrue、失敗したらfalse
		 */
		bool SignalFence();

		/**
		 * @brief フェンスの値がGPUの処理が終わった値になるまで待機する
		 * @param fenceValue 待つフェンスの値
		 * @return GPUの処理が完了したらtrue、失敗したらfalse
		 */
		bool WaitForFenceValue(UINT64 fenceValue);

		/**
		 * @brief バックバッファの状態を変える
		 * @param before 変える前の状態
		 * @param after 変えた後の状態
		 */
		void TransitionBackBuffer(D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);


	private:
		Microsoft::WRL::ComPtr<ID3D12Device>       m_device;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
		Microsoft::WRL::ComPtr<IDXGISwapChain3>    m_swapChain;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetViewDescriptorHeap;
		Microsoft::WRL::ComPtr<ID3D12Resource>       m_swapChainBufferResources[RENDER_TARGET_COUNT];
		D3D12_CPU_DESCRIPTOR_HANDLE                  m_renderTargetViewDescriptorHeapHandle{};
		UINT                                         m_descriptorHandleIncrementSize = 0;
		UINT                                         m_backBufferIndex = 0;                           // 現在描画中のバッファのインデックス

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_depthStencilViewDescriptorHeap;
		Microsoft::WRL::ComPtr<ID3D12Resource>       m_depthStencilBufferResource;

		D3D12_VIEWPORT m_viewport{};         // 描き先のどの範囲に描くか
		D3D12_RECT     m_scissorRectangle{}; // この四角の外は描かない

		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_commandAllocators[RENDER_TARGET_COUNT];
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

		Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;                                      // GPUが届いた番号を持つ
		UINT64                              m_frameFenceValues[RENDER_TARGET_COUNT] = {}; // Allocatorごとの最後のSignalの値。Resetの前にここまで待つ。
		UINT64                              m_fenceValue = 0;                             // CPUが最後に発行した番号
		HANDLE                              m_fenceEvent = nullptr;                       // 番号に届いたときにCPUを起こす合図

		CpuProfiler m_cpuProfiler; // 待ちを除いたCPUの処理時間を測る
		GpuProfiler m_gpuProfiler; // GPUの処理時間を測る

		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_uploadBuffers; // コピーを終えるまで預かるUPLOADのバッファ
	};
} // namespace fangEngine::render
