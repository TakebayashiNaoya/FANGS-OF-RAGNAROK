/**
 * @file LevelGrowthTests.cpp
 * @brief LevelGrowth（経験値の加算・段の境目・1 回で複数段・上限での頭打ち・倍率）のテスト。
 */
#include "Scene/LevelGrowth.h"
#include <doctest.h>


TEST_CASE("LevelProgress: 既定値はレベル1・経験値0")
{
	const fang::LevelProgress progress;
	CHECK(progress.level == 1);
	CHECK(progress.experiencePoints == 0);
}


TEST_CASE("AddExperience: 経験値0以下では持ち点もレベルも動かない")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	CHECK(fang::AddExperience(parameter, 0, &progress).gainedLevelCount == 0);
	CHECK(fang::AddExperience(parameter, -10, &progress).gainedLevelCount == 0);
	CHECK(progress.level == 1);
	CHECK(progress.experiencePoints == 0);
}


TEST_CASE("AddExperience: 撃破1件ぶん(10点)で経験値が10増え、レベルは変わらない")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	const fang::LevelGrowthResult result = fang::AddExperience(parameter, 10, &progress);
	CHECK(result.gainedLevelCount == 0);
	CHECK(progress.level == 1);
	CHECK(progress.experiencePoints == 10);
}


TEST_CASE("AddExperience: 同じフレームに3体ぶん(30点)入れても、必要量40に届かなければレベルは変わらない")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	const fang::LevelGrowthResult result = fang::AddExperience(parameter, 30, &progress);
	CHECK(result.gainedLevelCount == 0);
	CHECK(progress.level == 1);
	CHECK(progress.experiencePoints == 30);
}


TEST_CASE("AddExperience: 累計4体(40点)でレベル2になり、持ち点は0に戻る")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	fang::LevelGrowthResult result{};
	for (int defeat = 0; defeat < 4; ++defeat)
	{
		result = fang::AddExperience(parameter, 10, &progress);
	}

	CHECK(result.gainedLevelCount == 1);
	CHECK(progress.level == 2);
	CHECK(progress.experiencePoints == 0);
}


TEST_CASE("AddExperience: 累計12体(120点)でレベル3になる")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	for (int defeat = 0; defeat < 12; ++defeat)
	{
		(void)fang::AddExperience(parameter, 10, &progress);
	}

	CHECK(progress.level == 3);
	CHECK(progress.experiencePoints == 0);
}


TEST_CASE("AddExperience: 累計40体でレベル5、180体でレベル10になる")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progressToLevel5;
	fang::LevelProgress              progressToLevel10;

	for (int defeat = 0; defeat < 40; ++defeat)
	{
		(void)fang::AddExperience(parameter, 10, &progressToLevel5);
	}
	CHECK(progressToLevel5.level == 5);

	for (int defeat = 0; defeat < 180; ++defeat)
	{
		(void)fang::AddExperience(parameter, 10, &progressToLevel10);
	}
	CHECK(progressToLevel10.level == 10);
	CHECK(progressToLevel10.experiencePoints == 0);
}


TEST_CASE("AddExperience: 1回で2段以上ぶん入れば一度に上がり、余りは次の段へ持ち越す")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	// Lv1->2 に40、Lv2->3 に80。200点のうち120を使ってLv3になり、余り80はLv3の必要量120に届かない。
	const fang::LevelGrowthResult result = fang::AddExperience(parameter, 200, &progress);
	CHECK(result.gainedLevelCount == 2);
	CHECK(progress.level == 3);
	CHECK(progress.experiencePoints == 80);
}


TEST_CASE("AddExperience: 上限レベルに達した後は経験値を足してもレベル10のまま")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress{ .level = 10, .experiencePoints = 0 };

	const fang::LevelGrowthResult result = fang::AddExperience(parameter, 100000, &progress);
	CHECK(result.gainedLevelCount == 0);
	CHECK(progress.level == 10);
}


TEST_CASE("AddExperience: 上限到達後に大量の経験値を積み続けても持ち点の整数が溢れない")
{
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress{ .level = 10, .experiencePoints = 0 };

	for (int addition = 0; addition < 1000; ++addition)
	{
		(void)fang::AddExperience(parameter, 100000, &progress);
	}

	CHECK(progress.level == 10);
	CHECK(progress.experiencePoints >= 0); // オーバーフローで負に転じていないこと。
	CHECK(progress.experiencePoints == fang::ComputeExperienceToNextLevel(parameter, 10));
}


TEST_CASE("ComputeStatusMultiplier: レベル1で1.0、レベル10で2.35になる")
{
	const fang::LevelGrowthParameter parameter;

	CHECK(fang::ComputeStatusMultiplier(parameter, 1) == doctest::Approx(1.0f));
	CHECK(fang::ComputeStatusMultiplier(parameter, 10) == doctest::Approx(2.35f));
}


TEST_CASE("ComputeStatusMultiplier: レベルを直接書き換えてもAddExperienceで到達したときと同じ倍率になる")
{
	const fang::LevelGrowthParameter parameter;

	fang::LevelProgress viaExperience;
	for (int defeat = 0; defeat < 40; ++defeat)
	{
		(void)fang::AddExperience(parameter, 10, &viaExperience);
	}
	CHECK(viaExperience.level == 5);

	fang::LevelProgress viaDirectWrite{ .level = 5 };

	CHECK(
		fang::ComputeStatusMultiplier(parameter, viaExperience.level) ==
		doctest::Approx(fang::ComputeStatusMultiplier(parameter, viaDirectWrite.level))
	);
}


TEST_CASE("ClampLevelProgress: 範囲外のレベル・経験値を書いても範囲内に収まる")
{
	const fang::LevelGrowthParameter parameter;

	fang::LevelProgress tooHigh{ .level = 999, .experiencePoints = -5 };
	fang::ClampLevelProgress(parameter, &tooHigh);
	CHECK(tooHigh.level == parameter.maximumLevel);
	CHECK(tooHigh.experiencePoints == 0);

	fang::LevelProgress tooLow{ .level = 0, .experiencePoints = 0 };
	fang::ClampLevelProgress(parameter, &tooLow);
	CHECK(tooLow.level == 1);
}


TEST_CASE("AddExperience: 上昇率0・上限レベル1・1体0点でも落ちず、レベルも持ち点も動かない")
{
	fang::LevelGrowthParameter parameter;
	parameter.statusRatePerLevel            = 0.0f;
	parameter.maximumLevel                  = 1;
	parameter.experienceToNextLevelPerLevel = 40;

	fang::LevelProgress progress;
	for (int defeat = 0; defeat < 10; ++defeat)
	{
		(void)fang::AddExperience(parameter, 0, &progress);
	}

	CHECK(progress.level == 1);
	CHECK(progress.experiencePoints == 0);
	CHECK(fang::ComputeStatusMultiplier(parameter, progress.level) == doctest::Approx(1.0f));
}


TEST_CASE("AddExperience: 必要経験値0でも1フレームで上限ちょうどまでしか上がらない")
{
	fang::LevelGrowthParameter parameter;
	parameter.experienceToNextLevelPerLevel = 0;
	parameter.maximumLevel                  = 10;

	fang::LevelProgress progress;

	const fang::LevelGrowthResult result = fang::AddExperience(parameter, 1, &progress);
	CHECK(result.gainedLevelCount == 9);
	CHECK(progress.level == 10);
	CHECK(progress.experiencePoints == 0);
}


TEST_CASE("AddExperience: 180体ぶんの撃破を1件ずつ流し込んでも完走し、Lv10でちょうど頭打ちになる")
{
	// LevelGrowthParameter / LevelProgress / LevelGrowthResult はどれも int32_t と float だけの POD で、
	// AddExperience はアロケータを受け取らず可変長の入れ物も持たない ➡ 構造としてヒープを確保できない。
	const fang::LevelGrowthParameter parameter;
	fang::LevelProgress              progress;

	int32_t totalGainedLevelCount = 0;
	for (int defeat = 0; defeat < 180; ++defeat)
	{
		totalGainedLevelCount += fang::AddExperience(parameter, 10, &progress).gainedLevelCount;
	}

	CHECK(totalGainedLevelCount == 9);
	CHECK(progress.level == 10);
}
