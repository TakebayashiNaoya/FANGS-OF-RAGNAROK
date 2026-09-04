/**
 * @file PlatformBudgetTests.cpp
 * @brief Xbox 予算のテスト。倍率の丸め、換算、予算超過の判定、制限したときの待ち。
 */
#include "Core/Platform/Budget.h"
#include "Core/Platform/MemoryUsage.h"
#include <doctest.h>
#include <chrono>
#include <cstdint>


namespace
{
	/** @brief 換算の確かめに使う実処理時間。60fps の予算のちょうど半分。 */
	constexpr float HALF_BUDGET_SECONDS = fang::budget::FRAME_BUDGET_SECONDS * 0.5f;

	/** @brief 待ちの確かめに使う実処理時間。短すぎると測定の粒度に埋もれる。 */
	constexpr float THROTTLE_WORK_SECONDS = 0.010f;

	/** @brief 待ちの確かめに使う倍率。10ms が 30ms になる。 */
	constexpr float THROTTLE_SCALE_FACTOR = 3.0f;


	/** @brief 関数を呼ぶのにかかった秒を測る。 */
	template <typename Function> [[nodiscard]] float MeasureSeconds(Function&& function)
	{
		const auto start = std::chrono::steady_clock::now();
		function();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}
} // namespace


TEST_CASE("倍率は決めた範囲で丸められる")
{
	fang::PlatformBudget budget;

	CHECK(budget.GetCpuScaleFactor() == doctest::Approx(1.0f));

	budget.SetCpuScaleFactor(4.5f);
	CHECK(budget.GetCpuScaleFactor() == doctest::Approx(4.5f));

	// Xbox のほうが速いことはないので、1 未満は下限で止める。
	budget.SetCpuScaleFactor(0.25f);
	CHECK(budget.GetCpuScaleFactor() == doctest::Approx(fang::budget::MINIMUM_CPU_SCALE_FACTOR));

	budget.SetCpuScaleFactor(1000.0f);
	CHECK(budget.GetCpuScaleFactor() == doctest::Approx(fang::budget::MAXIMUM_CPU_SCALE_FACTOR));
}


TEST_CASE("実処理時間は倍率どおりに換算される")
{
	fang::PlatformBudget budget;

	budget.SetCpuScaleFactor(1.0f);
	budget.EndFrame(HALF_BUDGET_SECONDS);

	CHECK(budget.GetFrameWorkSeconds() == doctest::Approx(HALF_BUDGET_SECONDS));
	CHECK(budget.GetScaledFrameSeconds() == doctest::Approx(HALF_BUDGET_SECONDS));

	// 倍率を上げても実処理時間そのものは変わらない。変わるのは換算した値だけ。
	budget.SetCpuScaleFactor(3.0f);
	CHECK(budget.GetFrameWorkSeconds() == doctest::Approx(HALF_BUDGET_SECONDS));
	CHECK(budget.GetScaledFrameSeconds() == doctest::Approx(HALF_BUDGET_SECONDS * 3.0f));
}


TEST_CASE("予算超過は換算した時間で判定する")
{
	fang::PlatformBudget budget;

	budget.SetCpuScaleFactor(1.0f);
	budget.EndFrame(HALF_BUDGET_SECONDS);
	CHECK_FALSE(budget.IsOverFrameBudget());

	// PC では予算の半分でも、Xbox で 3 倍かかるなら 60fps に入らない。
	budget.SetCpuScaleFactor(3.0f);
	CHECK(budget.IsOverFrameBudget());
}


TEST_CASE("制限を入れたときだけ換算時間まで待つ")
{
	fang::PlatformBudget budget;
	budget.SetCpuScaleFactor(THROTTLE_SCALE_FACTOR);

	SUBCASE("切っていれば待たない")
	{
		budget.SetThrottleEnabled(false);

		const float elapsedSeconds = MeasureSeconds([&budget]() { budget.EndFrame(THROTTLE_WORK_SECONDS); });
		CHECK(elapsedSeconds < THROTTLE_WORK_SECONDS);
	}

	SUBCASE("入れていれば足りない分を待つ")
	{
		budget.SetThrottleEnabled(true);

		// 実処理はもう終わっている前提なので、待つのは換算時間との差の 20ms。
		const float expectedWaitSeconds = THROTTLE_WORK_SECONDS * (THROTTLE_SCALE_FACTOR - 1.0f);
		const float elapsedSeconds      = MeasureSeconds([&budget]() { budget.EndFrame(THROTTLE_WORK_SECONDS); });

		// 上振れは OS の都合で起きるので下限だけ見る。
		CHECK(elapsedSeconds >= expectedWaitSeconds);
	}
}


TEST_CASE("メモリ使用量を OS から取れる")
{
	const fang::MemoryUsage usage = fang::GetProcessMemoryUsage();

	// 動いている以上 0 ではない。上限は PC では分からないので 0 が返る。
	CHECK(usage.usedBytes > 0);
	CHECK(usage.usedBytes < fang::budget::MEMORY_LIMIT_BYTES);
}
