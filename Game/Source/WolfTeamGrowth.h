/**
 * @file WolfTeamGrowth.h
 * @brief 狼のチームの経験値・レベル・ステータス倍率。GameRules 4 のチームレベル制。
 */
#pragma once

#include "Core/Reflection/Reflection.h"
#include "Scene/LevelGrowth.h"
#include <cstdint>


namespace fang::game
{
	/**
	 * @brief 狼のチームの成長。ラン内だけの値で、保存も引き継ぎもしない。
	 * @details 実体は WolfManager が 1 つだけ持ち、狼はポインタで借りる（GameRules 4 のチームレベル制
	 *          ➡ 席ごとの複製を作らない）。撃破の申告と倍率がここで折り返す。
	 * @threading 更新ジョブ 1 本から。つまみの書き戻しはフレームループが更新の外で入れる（ADR-051）。
	 */
	struct WolfTeamGrowth
	{
		FANG_REFLECT_BEGIN(WolfTeamGrowth)
		FANG_FIELD_NESTED(levelGrowth, "上がり方")
		FANG_FIELD_NESTED(levelProgress, "今の値")
		FANG_FIELD(experiencePerDefeat, "撃破 1 件の経験値", Range(0.0f, 100.0f))
		FANG_FIELD(baseMaximumHitPoints, "基準の最大 HP", Range(1.0f, 10000.0f))
		FANG_REFLECT_END()

		LevelGrowthParameter levelGrowth;
		LevelProgress        levelProgress;

		/** @brief 雑魚 1 体の点。何点かはゲームの取り決めなので Game に置く。 */
		int32_t experiencePerDefeat = 10;

		/** @brief レベル 1 のときの最大 HP。雑魚の攻撃力 25 に対して 300 ➡ 12 発で倒れる。 */
		float baseMaximumHitPoints = 300.0f;

		/** @brief 直近の WolfManager::Update が出した倍率。狼が攻撃力に掛ける。つまみには出さない。 */
		float statusMultiplier = 1.0f;

		/** @brief 狼が積む撃破の件数。WolfManager::Update が周の頭で空にする。つまみには出さない。 */
		int32_t pendingDefeatCount = 0;
	};
} // namespace fang::game
