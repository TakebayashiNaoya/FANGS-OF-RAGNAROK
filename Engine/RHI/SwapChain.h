/**
 * @file SwapChain.h
 * @brief バックバッファの束と、その描画先ビュー。
 */
#pragma once

#include "RHI/D3D12Common.h"


namespace fang::rhi
{
	/**
	 * @brief スワップチェーンとバックバッファ一式。
	 * @details バックバッファ・RTV ヒープ・今描いている番号・画面の大きさをまとめて持つ。
	 *          GPU 待ちは持たない。Resize の前に待つのは呼び出し側の責務。
	 * @threading メインスレッドのみ。
	 */
	class SwapChain
	{
	public:
		[[nodiscard]] ID3D12Resource* GetCurrentBackBuffer() const { return m_backBuffers[m_frameIndex].Get(); }
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView() const;

		[[nodiscard]] uint32_t GetWidth() const { return m_width; }
		[[nodiscard]] uint32_t GetHeight() const { return m_height; }
		[[nodiscard]] uint32_t GetFrameIndex() const { return m_frameIndex; }


	public:
		/**
		 * @brief スワップチェーンとバックバッファの RTV を作る。
		 * @param windowHandle Windows なら HWND、UWP なら CoreWindow の IUnknown*。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(
			IDXGIFactory6&      factory,
			ID3D12Device&       device,
			ID3D12CommandQueue& commandQueue,
			void*               windowHandle,
			uint32_t            width,
			uint32_t            height
		);

		/**
		 * @brief バックバッファを作り直す。
		 * @details 呼ぶ前に GPU の完了を待っておくこと。参照が残っていると ResizeBuffers が失敗する。
		 */
		void Resize(ID3D12Device& device, uint32_t width, uint32_t height);

		/** @brief バックバッファと RTV を手放す。二重に呼んでも安全。 */
		void Shutdown();

		/** @brief 今のバックバッファを画面に出す。 */
		void Present();

		/** @brief 次に描くバックバッファの番号を取り直す。Present の後に呼ぶ。 */
		void UpdateFrameIndex();


	private:
		/** @brief バックバッファを取り直して RTV を張り直す。Initialize と Resize で共通。 */
		[[nodiscard]] bool CreateRenderTargetViews(ID3D12Device& device);


	private:
		ComPtr<IDXGISwapChain3>      m_swapChain;            /**< バックバッファの束。Present で画面に出す。 */
		ComPtr<ID3D12DescriptorHeap> m_renderTargetViewHeap; /**< バックバッファ用 RTV の置き場。 */
		ComPtr<ID3D12Resource>       m_backBuffers[BACK_BUFFER_COUNT]; /**< 描画先。m_frameIndex が指す 1 枚に描く。 */

		uint32_t m_renderTargetViewSize = 0; /**< RTV 1 個分のバイト数。GPU ごとに違う。 */

		uint32_t m_frameIndex = 0; /**< 今描いているバックバッファの番号。 */
		uint32_t m_width      = 0; /**< バックバッファの幅（ピクセル）。 */
		uint32_t m_height     = 0; /**< バックバッファの高さ（ピクセル）。 */
	};
} // namespace fang::rhi
