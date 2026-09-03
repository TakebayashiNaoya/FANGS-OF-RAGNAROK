/**
 * @file TexturePool.h
 * @brief テクスチャの台帳。
 */
#pragma once

#include "RHI/D3D12Common.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/GPUFence.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
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

			/**
			 * @brief 深度テクスチャのときだけ持つ DSV 1 個の置き場。サンプル専用のテクスチャでは空。
			 * @details 描画先になれるテクスチャは起動時のシャドウマップ 1 枚だけなので、共有ヒープを
			 *          用意して枠を配るより、エントリごとに 1 個ぶんのヒープを持つほうが素直。
			 */
			ComPtr<ID3D12DescriptorHeap> depthStencilViewHeap;
		};

		/** @brief ハンドルから中身を引く。無効・解放済みならアサートに掛かる。 */
		[[nodiscard]] const Entry& Get(TextureHandle handle) const;


	public:
		/**
		 * @brief テクスチャを作って全ミップを転送し、SRV を張る。
		 * @param source 形式と段ごとの中身。この呼び出しの間だけ読む。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] TextureHandle Create(
			ID3D12Device&        device,
			ID3D12CommandQueue&  commandQueue,
			GPUFence&            fence,
			DescriptorHeap&      descriptorHeap,
			const TextureSource& source
		);

		/**
		 * @brief 深度を書き込めて、書いた後はシェーダから読めるテクスチャを作る。
		 * @details 中身はその場で描くのでアップロードが要らない ➡ コマンドキューもフェンスも要らない。
		 * @param width  横のテクセル数。
		 * @param height 縦のテクセル数。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] TextureHandle CreateDepth(
			ID3D12Device&   device,
			DescriptorHeap& descriptorHeap,
			uint32_t        width,
			uint32_t        height
		);

		/** @brief スロットを空きに戻す。無効・解放済みのハンドルなら何もしない。 */
		void Destroy(TextureHandle handle);

		/** @brief 台帳ごと捨てる。二重に呼んでも安全。 */
		void Shutdown();


	private:
		/** @brief 出来上がったエントリを空きスロットか末尾へ置き、そのハンドルを返す。 */
		[[nodiscard]] TextureHandle Register(const Entry& entry);


	private:
		// TODO: Core の Array<T> とプールができたら差し替える。
		std::vector<Entry> m_entries; /**< TextureHandle.index で引く台帳。 */
	};
} // namespace fang::rhi
