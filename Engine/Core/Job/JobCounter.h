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
	 *          IsComplete() が true を返すか Wait が戻ったら、いつ壊してもよい。残り数が 0 になった後も
	 *          最後に減らしたスレッドは保留リストの引き取りで中身に触るので、完了はそれを終えてから公開する。
	 * @threading 任意のスレッド（内部で同期する）。
	 */
	class JobCounter
	{
	public:
		FANG_NON_COPYABLE(JobCounter);
		FANG_NON_MOVABLE(JobCounter);

		JobCounter() = default;

		/**
		 * @brief 残り数。
		 * @details 内部の同期用で、0 でも完了が公開されたとは限らない。壊してよいかの判定には IsComplete() を使う。
		 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetValue() const
		{
			return static_cast<uint32_t>(m_state.load(std::memory_order_acquire) & REMAINING_COUNT_MASK);
		}

		/**
		 * @brief 完了が公開されたか。
		 * @details 真ならもう誰もこのカウンタに触らないので壊してよい。まだ Submit していないカウンタも真。
		 */
		[[nodiscard]] FANG_FORCEINLINE bool IsComplete() const
		{
			return (m_state.load(std::memory_order_acquire) & COMPLETION_FLAG) != 0;
		}


	private:
		friend class JobSystem;

		/** @brief 保留リストが空であることを表す添字。 */
		static constexpr uint32_t INVALID_JOB_INDEX = 0xFFFFFFFFu;

		/** @brief 先頭が空の保留リスト。通し番号は 0 から始める。 */
		static constexpr uint64_t EMPTY_PENDING_HEAD = static_cast<uint64_t>(INVALID_JOB_INDEX);

		/** @brief 状態のうち残り数が使う範囲。 */
		static constexpr uint64_t REMAINING_COUNT_MASK = 0x00000000FFFFFFFFull;

		/** @brief 保留リストを引き取って完了を公開する役が決まっている。 */
		static constexpr uint64_t DISPATCH_OWNER_FLAG = 0x0000000100000000ull;

		/** @brief 完了を公開した。 */
		static constexpr uint64_t COMPLETION_FLAG = 0x0000000200000000ull;

		/** @brief 通し番号が使う範囲。 */
		static constexpr uint64_t SUBMIT_SERIAL_MASK = 0xFFFFFFFC00000000ull;

		/** @brief 通し番号を 1 つ進める幅。 */
		static constexpr uint64_t SUBMIT_SERIAL_UNIT = 0x0000000400000000ull;

		/**
		 * @brief 残り数・2 つのフラグ・Submit の通し番号を詰めた状態。作った直後は「0 件で公開済み」。
		 * @details 別々のアトミックにすると「0 になったか」と「公開してよいか」を 1 回の CAS で決められず、
		 *          前の代の引き取り役が完了を立て直して、走っているジョブを置いて Wait が抜ける。
		 *          通し番号まで同じ語に入れるのは、「0 ➡ 積む ➡ また 0」で残り数とフラグが元に戻り、
		 *          公開しようとしている引き取り役が「何も変わっていない」と誤認するのを防ぐため。
		 */
		std::atomic<uint64_t> m_state{ COMPLETION_FLAG };

		/**
		 * @brief 自分の 0 到達を待っているジョブの片方向リスト。
		 * @details 下位 32 bit が先頭の添字、上位 32 bit が ABA よけの通し番号。
		 */
		std::atomic<uint64_t> m_pendingHead{ EMPTY_PENDING_HEAD };
	};
} // namespace fang
