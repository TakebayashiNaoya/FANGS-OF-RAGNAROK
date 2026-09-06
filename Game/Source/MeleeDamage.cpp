/**
 * @file MeleeDamage.cpp
 * @brief 近接攻撃の掃引が返した当たりを HP へ流し、撃破を Scene へ反映する。
 */
#include "MeleeDamage.h"
#include "Scene/ComponentTypes.h"


namespace fang::game
{
	MeleeDamageResult ApplyMeleeHits(Actor self, std::span<const SweepHit> hits, float attackPower)
	{
		MeleeDamageResult result;

		for (const SweepHit& hit : hits)
		{
			Actor target = self.GetActorFromIndex(hit.userIndex);
			if (!target.IsValid() || target.IsPendingDestroy())
			{
				continue;
			}

			HealthComponent* health = target.GetHealthComponent();
			if (health == nullptr)
			{
				continue;
			}

			if (ApplyDamage(health, attackPower).wasDefeated)
			{
				target.Destroy();
				++result.defeatedCount;
			}
		}

		return result;
	}
} // namespace fang::game
