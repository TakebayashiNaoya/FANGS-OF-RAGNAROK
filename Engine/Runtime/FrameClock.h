/**
 * @file FrameClock.h
 * @brief 実時間を測り、1 周の刻みに上限を掛ける。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <cstdint>


namespace fang
{
	/**
	 * @brief 1 周ぶんの時間。
	 * @details 刻みは上限で切ってあり、経過は切っていない。この 2 本だけが更新と描画へ渡る（ADR-043）。
	 */
	struct FrameTime
	{
		float  deltaTimeSeconds = 0.0f;  /**< 1 周の刻み。MAXIMUM_DELTA_TIME_SECONDS 以下、かつ 0 以上。 */
		double elapsedSeconds   = 0.0;   /**< Start からの経過。切っていない実時間の積算。 */
		bool   wasClamped       = false; /**< この周が上限で切られたか。 */
	};

	/**
	 * @brief 1 周の刻みの上限（秒）。
	 * @details 上限 × 狼の最大速度（400cm/秒）が狼のカプセル半径（18.15cm）を下回ることから、
	 *          33.3ms（30fps）〜45.4ms の間で決める。1/25 秒は区切りの良い fps 分の 1 で、
	 *          上下どちらにも余裕が残る（設計.md「上限の値」）。
	 */
	inline constexpr float MAXIMUM_DELTA_TIME_SECONDS = 1.0f / 25.0f;

	/**
	 * @brief 実時間を測り、刻みに上限を掛ける。
	 * @details <chrono> をヘッダに出さないため、基準時刻は steady_clock::time_point の生の表現で持つ。
	 *          EngineContext 経由でエディタのパネルまで届くヘッダなので、そこに <chrono> を通したくない。
	 * @threading メインスレッドのみ。フレームループが 1 周に 1 回 Tick する。
	 */
	class FrameClock
	{
	public:
		FrameClock()  = default;
		~FrameClock() = default;

		/** @brief 基準の時刻を今に据える。フレームループへ入る直前に 1 回。 */
		void Start();

		/** @brief 実時間を測って 1 周ぶん進める。 */
		[[nodiscard]] FrameTime Tick();

		/**
		 * @brief 与えた秒で 1 周ぶん進める。時計を使わない経路。
		 * @param rawDeltaTimeSeconds 測った実時間。負でも NaN でもよい（0 へ落とす）。
		 * @details Tick の中身そのもの。テストと、決まった刻みで回したいとき用。
		 */
		[[nodiscard]] FrameTime Advance(double rawDeltaTimeSeconds);

		/** @brief 上限で切られた周の数。通常再生では 0 のまま。 */
		[[nodiscard]] uint32_t GetClampedFrameCount() const { return m_clampedFrameCount; }

		/** @brief Start からの経過（秒）。切っていない実時間。 */
		[[nodiscard]] double GetElapsedSeconds() const { return m_elapsedSeconds; }


	private:
		int64_t m_previousTicks = 0; /**< steady_clock::time_point::time_since_epoch().count()。 */

		double   m_elapsedSeconds    = 0.0;
		uint32_t m_clampedFrameCount = 0;
	};
} // namespace fang
