/**
 * @file MeleeDamage.h
 * @brief 近接攻撃の掃引が返した当たりを HP へ流し、撃破を Scene へ反映する。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Scene/Actor.h"
#include <cstdint>
#include <span>


namespace fang::game
{
	/** @brief 当たりを HP へ流した結果。 */
	struct MeleeDamageResult
	{
		uint32_t defeatedCount = 0; /**< この呼び出しで HP を 0 以下にした相手の数。 */
	};

	/**
	 * @brief 掃引が返した当たりを HP へ流し、0 以下になった相手を Scene から消す。
	 * @param self 攻撃した側の窓。相手の窓を同じ Scene から作るためだけに使う（攻撃側の値は読まない）。
	 * @param hits StepMeleeSwing が書いた「この振りで初めて当たった相手」。
	 * @details HealthComponent を持たない相手と、破棄を予約済みの相手は黙って飛ばす。
	 *          ログは出さない（毎フレーム起きうる）。撃破が何を意味するか（Scene から消す）は
	 *          ゲームの都合なので Game に置く（ADR-035）。誰が撃破したかは載せない（GameRules 4）。
	 * @threading 更新ジョブ 1 本から。振る舞いの Update の中。
	 */
	[[nodiscard]] MeleeDamageResult ApplyMeleeHits(Actor self, std::span<const SweepHit> hits, float attackPower);
} // namespace fang::game
