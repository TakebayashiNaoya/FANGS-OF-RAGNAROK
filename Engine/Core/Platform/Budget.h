/**
 * @file Budget.h
 * @brief Xbox One の割り当てを PC でも守るための予算と、その現在値。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <cstdint>


namespace fang
{
	/**
	 * @brief Xbox One の開発者モード・Game 分類でアプリに割り当てられる量。
	 * @details PC で動かすときも同じ値で判定する。
	 *          実機より広い環境で作っていると、実機に載せて初めて足りないと分かることになるため。
	 */
	namespace budget
	{
		/** @brief 使えるメモリの上限。Game 分類の 5GB。App 分類で配置してしまうと 1GB になる。 */
		inline constexpr uint64_t MEMORY_LIMIT_BYTES = 5ull * 1024ull * 1024ull * 1024ull;

		/** @brief 使えるコアの数。8 コアのうち占有 4 + 共有 2。 */
		inline constexpr uint32_t USABLE_CORE_COUNT = 6;

		/** @brief 1 フレームに使える時間。60fps。 */
		inline constexpr float FRAME_BUDGET_SECONDS = 1.0f / 60.0f;

		/** @brief メモリがこの割合を超えたら警告を出す。 */
		inline constexpr float MEMORY_WARNING_RATIO = 0.8f;

		/**
		 * @brief Xbox が PC の何倍の時間をかけるか。2026-09-04 に実測した値。
		 * @details JobSystemPanel のテスト負荷（要素数 1048576 / 分割幅 256）の直列の移動平均を
		 *          両方で測って割った。実機 1.150 ms ÷ ノート 0.180 ms = 6.39。
		 *          実機は Xbox One S、ノートは i7-13620H。総和の検算は両方で一致していた。
		 *          この負荷は自動ベクトル化が効くので、分岐の多いコードでは差がもっと縮む。
		 *          ➡上限側の目安として使う。実機計測の代わりにはならない。
		 */
		inline constexpr float MEASURED_CPU_SCALE_FACTOR = 6.39f;

		/**
		 * @brief 起動時の倍率。
		 * @details 実機で換算しても意味が無いので実機は 1.0。PC は実測値から始める。
		 */
#if FANG_TARGET_XBOX
		inline constexpr float DEFAULT_CPU_SCALE_FACTOR = 1.0f;
#else
		inline constexpr float DEFAULT_CPU_SCALE_FACTOR = MEASURED_CPU_SCALE_FACTOR;
#endif

		/** @brief CPU 倍率の下限。1.0 は「換算しない」。Xbox が PC より速いことはないので下は切る。 */
		inline constexpr float MINIMUM_CPU_SCALE_FACTOR = 1.0f;

		/** @brief CPU 倍率の上限。スライダで動かせる範囲の端。 */
		inline constexpr float MAXIMUM_CPU_SCALE_FACTOR = 20.0f;
	} // namespace budget


	/**
	 * @brief 予算に対する現在値を毎フレーム測り、超えたら知らせる。制限を入れると換算時間まで待たせる。
	 * @details CPU 倍率は「同じ処理を Xbox でやると何倍かかるか」。JobSystemPanel のテスト負荷を
	 *          実機と PC の両方で回し、所要時間の比を測って入れる。
	 *          倍率は 1 つの数でしかないので、SIMD が効く処理と分岐だらけの処理では実際の差が違う。
	 *          GPU 律速の部分にも効かない。目安であって実機計測の代わりにはならない。
	 * @threading メインスレッドのみ。フレームループから 1 フレームに 1 回呼ぶ。
	 */
	class PlatformBudget
	{
	public:
		FANG_NON_COPYABLE(PlatformBudget);

		PlatformBudget()  = default;
		~PlatformBudget() = default;

		/**
		 * @brief フレームの終わりに呼ぶ。換算して控え、制限が有効なら換算時間まで待つ。
		 * @param frameWorkSeconds このフレームの実処理にかかった秒。前のフレームの待ち時間は含めないこと。
		 */
		void EndFrame(float frameWorkSeconds);


	public:
		/** @brief 直近のフレームの実処理時間（秒）。待ち時間は含まない。 */
		[[nodiscard]] float GetFrameWorkSeconds() const { return m_frameWorkSeconds; }

		/** @brief 直近のフレームを Xbox 換算した時間（秒）。実処理時間 × 倍率。 */
		[[nodiscard]] float GetScaledFrameSeconds() const { return m_frameWorkSeconds * m_cpuScaleFactor; }

		/** @brief 換算時間が 60fps の予算を超えているか。 */
		[[nodiscard]] bool IsOverFrameBudget() const { return GetScaledFrameSeconds() > budget::FRAME_BUDGET_SECONDS; }

		/** @brief 直近に測ったメモリ使用量（バイト）。まだ測れていなければ 0。 */
		[[nodiscard]] uint64_t GetMemoryUsedBytes() const { return m_memoryUsedBytes; }

		/** @brief 起動してからのメモリ使用量の最高水位（バイト）。 */
		[[nodiscard]] uint64_t GetMemoryPeakBytes() const { return m_memoryPeakBytes; }

		/** @brief OS が申告した上限（バイト）。分からなければ 0。PC では実機よりずっと広い値が返る。 */
		[[nodiscard]] uint64_t GetSystemMemoryLimitBytes() const { return m_systemMemoryLimitBytes; }


	public:
		/** @brief Xbox が何倍の時間をかけるか。1.0 は未計測（換算しない）。 */
		[[nodiscard]] float GetCpuScaleFactor() const { return m_cpuScaleFactor; }

		/** @brief 倍率を入れる。範囲外は端で丸める。 */
		void SetCpuScaleFactor(float scaleFactor);

		/** @brief 換算時間までフレームを待たせるか。 */
		[[nodiscard]] bool IsThrottleEnabled() const { return m_isThrottleEnabled; }

		/** @brief 制限の入切。切り替えた直後の 1 フレームは経過時間が飛ぶ。 */
		void SetThrottleEnabled(bool isEnabled) { m_isThrottleEnabled = isEnabled; }


	private:
		/** @brief メモリ使用量を測り直し、予算を超えていたら警告を出す。 */
		void SampleMemoryUsage();

		/** @brief 換算時間に届くまで待つ。 */
		void WaitForScaledFrame(float frameWorkSeconds) const;


	private:
		float m_frameWorkSeconds = 0.0f;                             /**< 直近のフレームの実処理時間。 */
		float m_cpuScaleFactor   = budget::DEFAULT_CPU_SCALE_FACTOR; /**< Xbox 換算の倍率。1.0 は換算しない。 */

		uint64_t m_memoryUsedBytes        = 0; /**< 直近に測った使用量。 */
		uint64_t m_memoryPeakBytes        = 0; /**< 使用量の最高水位。 */
		uint64_t m_systemMemoryLimitBytes = 0; /**< OS が申告した上限。0 は分からなかったということ。 */

		uint32_t m_framesUntilMemorySample = 0; /**< 次にメモリを測るまでの残りフレーム数。 */

		bool m_isThrottleEnabled = false; /**< 換算時間まで待たせるか。 */

		bool m_hasWarnedMemoryNearLimit = false; /**< 8 割の警告を出したか。下がったら戻す。 */
		bool m_hasWarnedMemoryOverLimit = false; /**< 超過の警告を出したか。下がったら戻す。 */
	};
} // namespace fang
