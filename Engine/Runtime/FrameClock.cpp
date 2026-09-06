/**
 * @file FrameClock.cpp
 * @brief 実時間を測る本体。基準時刻の保持と、刻みへの上限の適用。
 */
#include "Pch.h"
#include "Runtime/FrameClock.h"
#include <chrono>


namespace fang
{
	void FrameClock::Start()
	{
		m_previousTicks = std::chrono::steady_clock::now().time_since_epoch().count();

		m_elapsedSeconds    = 0.0;
		m_clampedFrameCount = 0;
	}


	FrameTime FrameClock::Tick()
	{
		const auto    currentTimePoint = std::chrono::steady_clock::now();
		const int64_t currentTicks     = currentTimePoint.time_since_epoch().count();

		const std::chrono::steady_clock::duration rawDuration{ currentTicks - m_previousTicks };
		const double rawDeltaTimeSeconds = std::chrono::duration<double>(rawDuration).count();

		m_previousTicks = currentTicks;

		return Advance(rawDeltaTimeSeconds);
	}


	FrameTime FrameClock::Advance(double rawDeltaTimeSeconds)
	{
		// 負と NaN をここで殺す。x < 0.0 だと NaN が素通りするので !(x > 0.0) にする。
		if (!(rawDeltaTimeSeconds > 0.0))
		{
			rawDeltaTimeSeconds = 0.0;
		}

		m_elapsedSeconds += rawDeltaTimeSeconds; // 経過は切る前を積む。

		const bool  wasClamped = rawDeltaTimeSeconds > static_cast<double>(MAXIMUM_DELTA_TIME_SECONDS);
		const float deltaTimeSeconds =
			wasClamped ? MAXIMUM_DELTA_TIME_SECONDS : static_cast<float>(rawDeltaTimeSeconds);

		if (wasClamped)
		{
			++m_clampedFrameCount;
		}

		return FrameTime{
			.deltaTimeSeconds = deltaTimeSeconds,
			.elapsedSeconds   = m_elapsedSeconds,
			.wasClamped       = wasClamped,
		};
	}
} // namespace fang
