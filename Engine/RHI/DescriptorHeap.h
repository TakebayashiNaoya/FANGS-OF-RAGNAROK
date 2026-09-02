/**
 * @file DescriptorHeap.h
 * @brief シェーダから見えるディスクリプタヒープ。
 */
#pragma once

#include "RHI/D3D12Common.h"


namespace fang::rhi
{
	/**
	 * @brief シェーダ可視の SRV 置き場。
	 * @details 先頭から順に配り、返却は受け付けない（リングバッファ化するまでの暫定）。
	 * @threading メインスレッドのみ。
	 */
	class DescriptorHeap
	{
	public:
		/** @brief コマンドリストに差すためのヒープ本体。 */
		[[nodiscard]] ID3D12DescriptorHeap* GetNative() const { return m_heap.Get(); }

		/** @brief ビューを書き込むための CPU 側のハンドル。 */
		[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index) const;

		/** @brief シェーダに渡すための GPU 側のハンドル。 */
		[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index) const;


	public:
		/**
		 * @brief ヒープを作る。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(ID3D12Device& device);

		/** @brief ヒープを手放す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief ディスクリプタを 1 個確保する。
		 * @param outIndex 確保できたときだけ位置が入る。
		 * @return 満杯なら false。
		 */
		[[nodiscard]] bool Allocate(uint32_t& outIndex);


	private:
		ComPtr<ID3D12DescriptorHeap> m_heap; /**< シェーダから見える SRV の置き場。 */

		uint32_t m_descriptorSize = 0; /**< SRV 1 個分のバイト数。 */
		uint32_t m_nextDescriptor = 0; /**< 次に使う SRV の位置。今は返却しないので増える一方。 */
	};
} // namespace fang::rhi
