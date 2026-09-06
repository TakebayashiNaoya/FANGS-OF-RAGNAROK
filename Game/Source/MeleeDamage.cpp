/**
 * @file MeleeDamage.cpp
 * @brief 近接攻撃の掃引が返した当たりを HP へ流し、撃破を Scene へ反映する。
 */
#include "MeleeDamage.h"


namespace fang::game
{
	void ApplyMeleeHits(Scene& scene, std::span<const SweepHit> hits, float attackPower)
	{
		for (const SweepHit& hit : hits)
		{
			const ActorHandle target = scene.GetHandleFromIndex(hit.userIndex);
			if (!target.IsValid() || scene.IsPendingDestroy(target))
			{
				continue;
			}

			HealthComponent* health = scene.GetHealthComponent(target);
			if (health == nullptr)
			{
				continue;
			}

			if (ApplyDamage(health, attackPower).wasDefeated)
			{
				scene.DestroyObject(target);
			}
		}
	}
} // namespace fang::game
