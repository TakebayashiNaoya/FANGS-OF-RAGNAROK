/**
 * @file RenderContext.h
 * @brief レンダーコンテキストクラス
 */
#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include "Core/Noncopyable.h"


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
		 * @brief 1フレーム分描画する
		 * @return 描画に成功したらtrue、失敗したらfalse
		 */
		bool RenderFrame();


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
		 * @brief フェンスの値がGPUの処理が終わった値になるまで待機する
		 * @param fenceValue 待つフェンスの値
		 * @return GPUの処理が完了したらtrue、失敗したらfalse
		 */
		bool WaitForFenceValue(UINT64 fenceValue);

		/**
		 * @brief GPUの処理が完了するまで待機する
		 * @return GPUの処理が完了したらtrue、失敗したらfalse
		 */
		bool WaitForGpu();


	private:
		static constexpr int RENDER_TARGET_COUNT = 2;

		Microsoft::WRL::ComPtr<ID3D12Device>       m_device;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
		Microsoft::WRL::ComPtr<IDXGISwapChain3>    m_swapChain;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetViewDescriptorHeap;
		Microsoft::WRL::ComPtr<ID3D12Resource>       m_swapChainBufferResources[RENDER_TARGET_COUNT];
		D3D12_CPU_DESCRIPTOR_HANDLE                  m_renderTargetViewDescriptorHeapHandle{};
		UINT                                         m_descriptorHandleIncrementSize = 0;

		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    m_commandAllocators[RENDER_TARGET_COUNT];
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

		Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;                                      // GPUが届いた番号を持つ
		UINT64                              m_frameFenceValues[RENDER_TARGET_COUNT] = {}; // Allocatorごとの最後のSignalの値。Resetの前にここまで待つ。
		UINT64                              m_fenceValue = 0;                             // CPUが最後に発行した番号
		HANDLE                              m_fenceEvent = nullptr;                       // 番号に届いたときにCPUを起こす合図
	};
} // namespace fangEngine::render
