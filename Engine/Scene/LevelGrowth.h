/**
 * @file LevelGrowth.h
 * @brief 経験値からレベル・ステータス倍率を出す、状態を持たない計算。
 * @details Scene クラスも Actor も include しない。整数と float しか受け取らないので、
 *          テストは Engine だけで回る（MeleeSwing / CameraOcclusion と同じ性格）。
 */
#pragma once

#include "Core/Reflection/Reflection.h"
#include <cstdint>


namespace fang
{
	/** @brief レベルの上がり方。 */
	struct LevelGrowthParameter
	{
		FANG_REFLECT_BEGIN(LevelGrowthParameter)
		FANG_FIELD(experienceToNextLevelPerLevel, "次の段までの経験値", Range(0.0f, 1000.0f))
		FANG_FIELD(maximumLevel, "上限レベル", Range(1.0f, 50.0f))
		FANG_FIELD(statusRatePerLevel, "1 段あたりの上昇率", Range(0.0f, 1.0f))
		FANG_REFLECT_END()

		/** @brief L から L+1 へ要る経験値は これ × L。40 なら Lv1➡2 が 40、Lv9➡10 が 360。 */
		int32_t experienceToNextLevelPerLevel = 40;

		int32_t maximumLevel = 10;

		/** @brief 基準値への加算の率。倍率は 1 + これ × (L - 1)。複利にしない。 */
		float statusRatePerLevel = 0.15f;
	};

	/**
	 * @brief 今のレベルと、その段で貯まった経験値。
	 * @details 経験値は累計ではなく段の中の持ち点。レベルを直接書き換えても矛盾する値が残らない
	 *          （つまみでレベルを動かす前提、ADR-056）。
	 */
	struct LevelProgress
	{
		FANG_REFLECT_BEGIN(LevelProgress)
		FANG_FIELD(level, "レベル", Range(1.0f, 10.0f))
		FANG_FIELD(experiencePoints, "経験値", Range(0.0f, 10000.0f))
		FANG_REFLECT_END()

		int32_t level            = 1;
		int32_t experiencePoints = 0;
	};

	/** @brief 経験値を足した結果。 */
	struct LevelGrowthResult
	{
		int32_t gainedLevelCount = 0; /**< この呼び出しで上がった段の数。0 なら上がっていない。 */
	};

	/** @brief L から L+1 へ要る経験値。上限レベルでは「これ以上要らない」量として同じ式の値を返す。 */
	[[nodiscard]] int32_t ComputeExperienceToNextLevel(const LevelGrowthParameter& parameter, int32_t level);

	/** @brief レベル L のときの基準値への倍率。1 + rate × (L - 1)。L が 1 以下なら 1.0。 */
	[[nodiscard]] float ComputeStatusMultiplier(const LevelGrowthParameter& parameter, int32_t level);

	/**
	 * @brief つまみで入った値を範囲へ収める。
	 * @details レベルを 1〜上限へ、経験値を 0 以上へ。上限レベルなら持ち点も頭打ちにする。
	 *          周の頭で毎回呼ぶ（つまみは上限レベルと無関係に 1〜10 を書けるため）。
	 */
	void ClampLevelProgress(const LevelGrowthParameter& parameter, LevelProgress* progress);

	/**
	 * @brief 経験値を足し、貯まった分だけ段を上げる。
	 * @param experiencePoints 足す点。0 以下なら何もしない。
	 * @details 1 回で 2 段以上ぶん入れば一度に上がり、余りは次の段へ残る。上限レベルでは持ち点を
	 *          頭打ちにして加算を止める ➡ 積み続けても整数が溢れない。
	 * @threading 更新ジョブ 1 本から。
	 */
	[[nodiscard]] LevelGrowthResult AddExperience(
		const LevelGrowthParameter& parameter,
		int32_t                     experiencePoints,
		LevelProgress*              progress
	);
} // namespace fang
