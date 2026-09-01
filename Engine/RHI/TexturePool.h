/**
 * @file TexturePool.h
 * @brief テクスチャの台帳。
 */
#pragma once

#include "RHI/D3D12Common.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/GPUFence.h"
#include "RHI/RHIHandles.h"
#include <vector>


namespace fang::rhi
{
	/**
	 * @brief TextureHandle で引くテクスチャの置き場。
	 * @details 解放したスロットは世代を進めて再利用する。古いハンドルは世代違いで弾ける。
	 * @threading メインスレッドのみ。生成の中で GPU の完了を待つので、フレームの外で呼ぶ。
	 */
	class TexturePool
	{
	public:
		struct Entry
		{
			ComPtr<ID3D12Resource> resource;                /**< テクスチャの実体。 */
			uint32_t               descriptorIndex = 0;     /**< シェーダ可視ヒープ上の位置。 */
			uint32_t               generation      = 0;     /**< ハンドルの世代と突き合わせる。 */
			bool                   isAlive         = false; /**< false なら空きスロット。次の生成で再利用される。 */
		};

		/** @brief ハンドルから中身を引く。無効・解放済みならアサートに掛かる。 */
		[[nodiscard]] const Entry& Get(TextureHandle handle) const;

		/**
		 * @brief テクスチャを作って中身を転送し、SRV を張る。
		 * @param pixels RGBA 各 8 bit のピクセル列。左上から右へ、行間の詰め物なし（1 行 = width * 4 バイト）。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] TextureHandle Create(
			ID3D12Device&       device,
			ID3D12CommandQueue& commandQueue,
			GPUFence&           fence,
			DescriptorHeap&     descriptorHeap,
			const void*         pixels,
			uint32_t            width,
			uint32_t            height
		);

		/** @brief スロットを空きに戻す。無効・解放済みのハンドルなら何もしない。 */
		void Destroy(TextureHandle handle);

		/** @brief 台帳ごと捨てる。二重に呼んでも安全。 */
		void Shutdown();


	private:
		// TODO: Core の Array<T> とプールができたら差し替える。
		std::vector<Entry> m_entries; /**< TextureHandle.index で引く台帳。 */
	};
} // namespace fang::rhi
