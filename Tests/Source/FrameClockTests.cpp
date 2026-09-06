/**
 * @file FrameClockTests.cpp
 * @brief FrameClock のテスト。刻みへの上限、負・NaN・0 の扱い、切られた周の数、経過秒の実時間追随。
 */
#include "Runtime/FrameClock.h"
#include <doctest.h>
#include <cmath>
#include <limits>


namespace
{
	/** @brief 狼の既定の移動速度（WolfMovementParameter::moveSpeedCentimetersPerSecond の既定値）。 */
	constexpr float WOLF_MAXIMUM_SPEED_CENTIMETERS_PER_SECOND = 400.0f;

	/** @brief 狼のカプセル半径（Wolf.gltf の POSITION 境界 Z 36.30 の半分）。 */
	constexpr float WOLF_CAPSULE_RADIUS_CENTIMETERS = 18.15f;

	static_assert(
		fang::MAXIMUM_DELTA_TIME_SECONDS * WOLF_MAXIMUM_SPEED_CENTIMETERS_PER_SECOND < WOLF_CAPSULE_RADIUS_CENTIMETERS,
		"上限 × 狼の最大速度が狼のカプセル半径を下回っていない。1 周で自分の体を通り抜けうる"
	);
} // namespace


TEST_CASE("Advance: 0 / 負 / NaN は刻み 0・切られていない扱いになる")
{
	fang::FrameClock clock;

	const fang::FrameTime zero = clock.Advance(0.0);
	CHECK(zero.deltaTimeSeconds == doctest::Approx(0.0f));
	CHECK_FALSE(zero.wasClamped);
	CHECK(zero.elapsedSeconds == doctest::Approx(0.0));

	const fang::FrameTime negative = clock.Advance(-1.0);
	CHECK(negative.deltaTimeSeconds == doctest::Approx(0.0f));
	CHECK_FALSE(negative.wasClamped);
	CHECK(negative.elapsedSeconds == doctest::Approx(0.0));

	const fang::FrameTime nan = clock.Advance(std::numeric_limits<double>::quiet_NaN());
	CHECK(nan.deltaTimeSeconds == doctest::Approx(0.0f));
	CHECK_FALSE(nan.wasClamped);
	CHECK(nan.elapsedSeconds == doctest::Approx(0.0));

	CHECK(clock.GetClampedFrameCount() == 0);
}


TEST_CASE("Advance: 上限を超える実時間は上限で切られ、切られた回数が増える")
{
	fang::FrameClock clock;

	const fang::FrameTime overLimit = clock.Advance(1.0);
	CHECK(overLimit.deltaTimeSeconds == doctest::Approx(fang::MAXIMUM_DELTA_TIME_SECONDS));
	CHECK(overLimit.wasClamped);
	CHECK(overLimit.elapsedSeconds == doctest::Approx(1.0));
	CHECK(clock.GetClampedFrameCount() == 1);

	const fang::FrameTime farOverLimit = clock.Advance(100.0);
	CHECK(farOverLimit.deltaTimeSeconds == doctest::Approx(fang::MAXIMUM_DELTA_TIME_SECONDS));
	CHECK(farOverLimit.wasClamped);
	CHECK(clock.GetClampedFrameCount() == 2);

	// 上限そのものはそのまま通る（境目はクランプ側に含めない）。
	const fang::FrameTime atLimit = clock.Advance(static_cast<double>(fang::MAXIMUM_DELTA_TIME_SECONDS));
	CHECK(atLimit.deltaTimeSeconds == doctest::Approx(fang::MAXIMUM_DELTA_TIME_SECONDS));
	CHECK_FALSE(atLimit.wasClamped);
	CHECK(clock.GetClampedFrameCount() == 2);
}


TEST_CASE("Advance: 60fps・30fps の刻みを1000周入れてもクランプが一切効かない")
{
	fang::FrameClock clock;

	constexpr double FPS_60_SECONDS = 1.0 / 60.0;
	constexpr double FPS_30_SECONDS = 1.0 / 30.0;

	for (uint32_t frame = 0; frame < 1000; ++frame)
	{
		(void)clock.Advance(FPS_60_SECONDS);
	}
	CHECK(clock.GetClampedFrameCount() == 0);

	fang::FrameClock clock30;
	for (uint32_t frame = 0; frame < 1000; ++frame)
	{
		(void)clock30.Advance(FPS_30_SECONDS);
	}
	CHECK(clock30.GetClampedFrameCount() == 0);
}


TEST_CASE("Advance: ゲーム内時刻は切られても実時間からずれない")
{
	fang::FrameClock clock;

	double clampedDeltaSum = 0.0;
	double rawSum          = 0.0;

	constexpr double NORMAL_FRAME_SECONDS = 1.0 / 60.0;
	constexpr double PAUSE_SECONDS        = 1.0; // ブレークポイントやウィンドウのドラッグに相当。

	for (uint32_t frame = 0; frame < 100; ++frame)
	{
		// 3 回だけ 1 秒の停止を混ぜる。
		const double rawDeltaTimeSeconds =
			(frame == 10 || frame == 40 || frame == 70) ? PAUSE_SECONDS : NORMAL_FRAME_SECONDS;

		const fang::FrameTime frameTime = clock.Advance(rawDeltaTimeSeconds);

		rawSum += rawDeltaTimeSeconds;
		clampedDeltaSum += frameTime.deltaTimeSeconds;

		CHECK(frameTime.elapsedSeconds == doctest::Approx(rawSum));
	}

	CHECK(clock.GetElapsedSeconds() == doctest::Approx(rawSum));
	CHECK(clock.GetClampedFrameCount() == 3);
	CHECK(clampedDeltaSum < rawSum);
}


TEST_CASE("Tick: Start の直後は経過が測定開始からの実時間になる")
{
	fang::FrameClock clock;
	clock.Start();

	const fang::FrameTime frameTime = clock.Tick();

	CHECK(frameTime.deltaTimeSeconds >= 0.0f);
	CHECK(frameTime.deltaTimeSeconds <= fang::MAXIMUM_DELTA_TIME_SECONDS);
	CHECK(frameTime.elapsedSeconds >= 0.0);
	CHECK(clock.GetElapsedSeconds() == doctest::Approx(frameTime.elapsedSeconds));
}
