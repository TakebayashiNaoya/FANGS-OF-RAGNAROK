/**
 * @file WorkStealingDeque.h
 * @brief ワークスティーリング用の両端キュー（Chase–Lev）。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <atomic>
#include <cstdint>


namespace fang
{
	/** @brief 別々のスレッドが触る変数を同じ行に載せないための境界。x64 と Jaguar はどちらも 64 バイト。 */
	inline constexpr size_t CACHE_LINE_SIZE = 64;

	/** @brief Steal の結果。 */
	enum class EnStealResult
	{
		Success, /**< 1 件取れた。 */
		Empty,   /**< 空だった。別の相手を当たる。 */
		Aborted  /**< 他のスレッドと競り負けた。もう一度同じ相手を当たってよい。 */
	};

	/**
	 * @brief 持ち主が底から出し入れし、他人が先頭から奪う固定長のキュー。
	 * @details Chase–Lev の deque。メモリオーダーは Lê らの弱いメモリモデル向けの版に合わせてある。
	 *          リングを伸ばさない代わりに、満杯なら Push が false を返す。
	 * @threading Push / Pop は持ち主のスレッドだけ。Steal は他の任意のスレッド。
	 */
	// C4324（アラインメント指定子のために構造体がパッドされた）は、ここでは狙って詰め物を入れている。
#pragma warning(push)
#pragma warning(disable : 4324)
	template <typename TValue, uint32_t TCapacity> class WorkStealingDeque
	{
		static_assert(TCapacity > 0 && (TCapacity & (TCapacity - 1)) == 0, "容量は 2 のべき乗であること");
		static_assert(std::atomic<TValue>::is_always_lock_free, "要素はロックフリーに読み書きできる大きさであること");


	public:
		FANG_NON_COPYABLE(WorkStealingDeque);
		FANG_NON_MOVABLE(WorkStealingDeque);

		WorkStealingDeque() = default;

		/** @brief 入っている数。目安であって、読んだ次の瞬間には変わっている。 */
		[[nodiscard]] uint32_t GetCount() const
		{
			const int64_t bottom = m_bottom.load(std::memory_order_relaxed);
			const int64_t top    = m_top.load(std::memory_order_relaxed);
			return bottom > top ? static_cast<uint32_t>(bottom - top) : 0;
		}


	public:
		/**
		 * @brief 底に積む。持ち主のスレッドだけが呼べる。
		 * @return 満杯なら false。積めていない。
		 */
		[[nodiscard]] bool Push(const TValue& value)
		{
			const int64_t bottom = m_bottom.load(std::memory_order_relaxed);
			const int64_t top    = m_top.load(std::memory_order_acquire);
			if (bottom - top > static_cast<int64_t>(TCapacity) - 1)
			{
				return false;
			}

			m_buffer[static_cast<uint64_t>(bottom) & INDEX_MASK].store(value, std::memory_order_relaxed);

			// 中身の書き込みを bottom の公開より前に見えさせる。これが無いと、奪う側が
			// 進んだ bottom を見た後で古い中身を読める。
			std::atomic_thread_fence(std::memory_order_release);
			m_bottom.store(bottom + 1, std::memory_order_relaxed);

			return true;
		}

		/**
		 * @brief 底から取る。持ち主のスレッドだけが呼べる。
		 * @param[out] outValue 取れたときだけ書き換える。
		 * @return 空だったか、最後の 1 件を奪われたら false。
		 */
		[[nodiscard]] bool Pop(TValue& outValue)
		{
			const int64_t bottom = m_bottom.load(std::memory_order_relaxed) - 1;
			m_bottom.store(bottom, std::memory_order_relaxed);

			// bottom を下げたことと top を読むことの順序を、奪う側から見ても入れ替わらないようにする。
			// 最後の 1 件を持ち主と奪う側が同時に取ってしまうのを防ぐのはこの 1 行。
			std::atomic_thread_fence(std::memory_order_seq_cst);

			int64_t top = m_top.load(std::memory_order_relaxed);
			if (top > bottom)
			{
				m_bottom.store(bottom + 1, std::memory_order_relaxed);
				return false;
			}

			const TValue value = m_buffer[static_cast<uint64_t>(bottom) & INDEX_MASK].load(std::memory_order_relaxed);
			if (top < bottom)
			{
				outValue = value;
				return true;
			}

			// 残り 1 件。奪う側と同じものを狙っているので、CAS に勝ったほうが取る。
			const bool hasWon =
				m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
			m_bottom.store(bottom + 1, std::memory_order_relaxed);
			if (!hasWon)
			{
				return false;
			}

			outValue = value;
			return true;
		}

		/**
		 * @brief 先頭から奪う。持ち主以外のスレッドから呼ぶ。
		 * @param[out] outValue Success のときだけ書き換える。
		 */
		[[nodiscard]] EnStealResult Steal(TValue& outValue)
		{
			int64_t top = m_top.load(std::memory_order_acquire);

			// top を読んでから bottom を読む順序を守る。逆に見えると、空でないのに空と判断する。
			std::atomic_thread_fence(std::memory_order_seq_cst);

			const int64_t bottom = m_bottom.load(std::memory_order_acquire);
			if (top >= bottom)
			{
				return EnStealResult::Empty;
			}

			const TValue value = m_buffer[static_cast<uint64_t>(top) & INDEX_MASK].load(std::memory_order_relaxed);
			if (!m_top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
			{
				return EnStealResult::Aborted;
			}

			outValue = value;
			return EnStealResult::Success;
		}


	private:
		static constexpr uint64_t INDEX_MASK = TCapacity - 1;

		// top と bottom は別のスレッドが叩き合うので、同じキャッシュラインに載せない。
		// 符号付きなのは、空のときの Pop で bottom が一時的に −1 になるため。符号なしだと
		// 巻き戻って「空でない」と判断してしまう。
		alignas(CACHE_LINE_SIZE) std::atomic<int64_t> m_top{ 0 };    /**< 奪う側が進める。 */
		alignas(CACHE_LINE_SIZE) std::atomic<int64_t> m_bottom{ 0 }; /**< 持ち主が進める。 */

		alignas(CACHE_LINE_SIZE) std::atomic<TValue> m_buffer[TCapacity]{};
	};
#pragma warning(pop)
} // namespace fang
