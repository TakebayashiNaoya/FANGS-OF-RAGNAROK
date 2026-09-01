/**
 * @file GPUFence.h
 * @brief GPU の進み具合を測るフェンス。
 */
#pragma once

#include "RHI/D3D12Common.h"


namespace fang::rhi
{
	/**
	 * @brief CPU から GPU の完了を待つためのフェンス。
	 * @details CPU と GPU は非同期に走るので、キューに積んだ仕事がいつ終わったかは
	 *          フェンス（GPU が値を書き込むカウンタ）越しにしか分からない。
	 * @threading メインスレッドのみ。
	 */
	class GPUFence
	{
	public:
		/**
		 * @brief フェンスと待機用のイベントを作る。
		 * @param device 生成元のデバイス。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(ID3D12Device& device);

		/** @brief 待機用のイベントを閉じ、フェンスを手放す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief 直前に積んだ分を GPU が消化するまで待つ。
		 * @details キューに積んだコマンドは積んだ瞬間には実行されておらず、GPU が自分のペースで消化する（CPU と GPU は非同期）。
		 *          この関数を抜けた時点で「ここまでに積んだ仕事は GPU 上で完全に終わっている」ことが保証される。
		 *          ただし CPU と GPU の並走を完全に止めるので高価。
		 *          使いどころは Resize / Shutdown / EndFrame（Phase 1 の割り切り）に限る。
		 */
		void WaitForGPU(ID3D12CommandQueue& commandQueue);


	private:
		ComPtr<ID3D12Fence> m_fence; /**< GPU の進み具合を知るカウンタ。 */

		uint64_t m_nextFenceValue = 1;       /**< 次に Signal する値。単調増加させる。 */
		HANDLE   m_fenceEvent     = nullptr; /**< フェンスの完了を待つための OS のイベント。 */
	};
} // namespace fang::rhi
