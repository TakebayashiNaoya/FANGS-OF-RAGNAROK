/**
 * @file DepthBuffer.h
 * @brief 深度バッファと、その深度ステンシルビュー。
 */
#pragma once

#include "RHI/D3D12Common.h"


namespace fang::rhi
{
	/**
	 * @brief 深度テスト用のテクスチャ 1 枚と DSV。
	 * @details 画面と同じ大きさで作る。SwapChain に持たせずに分けてあるのは、SwapChain の責務を
	 *          「バックバッファの束」に保つため。両方を持って作り直すのは GraphicsDevice の役目。
	 * @threading メインスレッドのみ。
	 */
	class DepthBuffer
	{
	public:
		/** @brief 深度バッファの形式。PSO の DSVFormat もこれに合わせる。 */
		static constexpr DXGI_FORMAT DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;

		/** @brief 深度バッファの DSV。OMSetRenderTargets にそのまま渡せる。Initialize 後にだけ呼べる。 */
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;


	public:
		/**
		 * @brief DSV のヒープと深度テクスチャを作る。
		 * @param width  バックバッファと同じ幅（ピクセル）。
		 * @param height バックバッファと同じ高さ（ピクセル）。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(ID3D12Device& device, uint32_t width, uint32_t height);

		/**
		 * @brief 深度テクスチャを作り直す。
		 * @details 呼ぶ前に GPU の完了を待っておくこと。まだ描き込んでいる最中のテクスチャを捨てることになる。
		 */
		void Resize(ID3D12Device& device, uint32_t width, uint32_t height);

		/** @brief 深度テクスチャと DSV を手放す。二重に呼んでも安全。 */
		void Shutdown();


	private:
		/** @brief 深度テクスチャを作って DSV を張る。Initialize と Resize で共通。 */
		[[nodiscard]] bool CreateDepthTexture(ID3D12Device& device, uint32_t width, uint32_t height);


	private:
		ComPtr<ID3D12DescriptorHeap> m_depthStencilViewHeap; /**< DSV 1 個の置き場。 */
		ComPtr<ID3D12Resource>       m_depthTexture;         /**< 深度の書き込み先。画面と同じ大きさ。 */
	};
} // namespace fang::rhi
