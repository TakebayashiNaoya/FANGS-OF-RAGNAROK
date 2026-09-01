/**
 * @file BufferPool.h
 * @brief 頂点・インデックスバッファの台帳。
 */
#pragma once

#include "RHI/GraphicsDevice.h"
#include "D3D12Common.h"
#include <vector>


namespace fang::rhi
{
	/**
	 * @brief BufferHandle で引くバッファの置き場。
	 * @details 解放したスロットは世代を進めて再利用する。古いハンドルは世代違いで弾ける。
	 * @threading メインスレッドのみ。
	 */
	class BufferPool
	{
	public:
		struct Entry
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

		/**
		 * @brief バッファを作って data を書き込む。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] BufferHandle Create(ID3D12Device& device,
										  const void*   data,
										  uint32_t      sizeInBytes,
										  uint32_t      strideInBytes,
										  EnBufferKind  kind);

		/**
		 * @brief 中身が空のバッファを作る。書き込みは Update で行う。
		 * @return 失敗したら無効なハンドル。
		 */
		[[nodiscard]] BufferHandle CreateDynamic(ID3D12Device& device,
												 uint32_t      capacityInBytes,
												 uint32_t      strideInBytes,
												 EnBufferKind  kind);

		/** @brief バッファの先頭から data を書き込む。容量を超えるとアサートに掛かる。 */
		void Update(BufferHandle handle, const void* data, uint32_t sizeInBytes);

		/** @brief スロットを空きに戻す。無効・解放済みのハンドルなら何もしない。 */
		void Destroy(BufferHandle handle);

		/** @brief ハンドルから中身を引く。無効・解放済みならアサートに掛かる。 */
		[[nodiscard]] const Entry& Get(BufferHandle handle) const;


	private:
		// TODO: Core の Array<T> とプールができたら差し替える。
		std::vector<Entry> m_entries; /**< BufferHandle.index で引く台帳。 */
	};
} // namespace fang::rhi
