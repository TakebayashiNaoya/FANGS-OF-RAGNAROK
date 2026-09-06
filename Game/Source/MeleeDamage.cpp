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
				// Destroy の1行前に控える。GetWorldPosition は直近のUpdateが組み立てたワールド行列を
				// 読むので、破棄反映(次のUpdate)より前のこの瞬間が最後の機会。
				result.defeatedPositions[result.defeatedCount] = target.GetWorldPosition();

				target.Destroy();
				++result.defeatedCount;
			}
		}

		return result;
	}
} // namespace fang::game
