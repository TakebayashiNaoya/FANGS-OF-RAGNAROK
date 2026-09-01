/**
 * @file JobCounter.h
 * @brief ジョブの完了数を数えるカウンタ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <atomic>
#include <cstdint>


namespace fang
{
	class JobSystem;

	/**
	 * @brief ジョブの残り数。0 になったら「そのひとまとまりが終わった」ことを表す。
	 * @details 呼び出し側がスタックかメンバに置き、Submit の finishCounter や JobDesc::waitCounter に渡す。
	 *          ファイバーに差し替えるときも「待つ相手はカウンタ」という形は変えずに済む。
	 *          Wait が戻り、かつ自分を待っていたジョブが全部積み直されるまでは壊さないこと。
	 *          最後の 1 個を減らしたスレッドが、0 にした直後まで中身に触る。
	 * @threading 任意のスレッド（内部で同期する）。
	 */
	class JobCounter
	{
	public:
		FANG_NON_COPYABLE(JobCounter);
		FANG_NON_MOVABLE(JobCounter);

		JobCounter() = default;

		/** @brief 残り数。0 なら完了。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetValue() const { return m_value.load(std::memory_order_acquire); }

		/** @brief 残り 0 かどうか。まだ Submit していないカウンタも 0 なので、積む前に問い合わせない。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsComplete() const { return GetValue() == 0; }


	private:
		friend class JobSystem;

		/** @brief 保留リストが空であることを表す添字。 */
		static constexpr uint32_t INVALID_JOB_INDEX = 0xFFFFFFFFu;

		/** @brief 先頭が空の保留リスト。通し番号は 0 から始める。 */
		static constexpr uint64_t EMPTY_PENDING_HEAD = static_cast<uint64_t>(INVALID_JOB_INDEX);

		std::atomic<uint32_t> m_value{ 0 };

		/**
		 * @brief 自分の 0 到達を待っているジョブの片方向リスト。
		 * @details 下位 32 bit が先頭の添字、上位 32 bit が ABA よけの通し番号。
		 */
		std::atomic<uint64_t> m_pendingHead{ EMPTY_PENDING_HEAD };
	};
} // namespace fang
