/**
 * @file ComponentTypes.cpp
 * @brief HealthComponent のダメージ処理（ApplyDamage / TickInvincibility）。
 */
#include "Pch.h"
#include "Scene/ComponentTypes.h"
#include <algorithm>


namespace fang
{
	namespace
	{
		/** @brief 1 フレームの引き算の丸め残り（1/167 秒）。これ以下は 0 へ落とす。 */
		constexpr float INVINCIBLE_EPSILON_SECONDS = 1.0e-4f;
	} // namespace


	DamageResult ApplyDamage(HealthComponent* health, float damage)
	{
		if (health->invincibleSecondsRemaining > 0.0f)
		{
			return DamageResult{};
		}

		health->currentHitPoints -= damage;
		health->invincibleSecondsRemaining = health->invincibleSeconds;

		return DamageResult{
			.wasApplied  = true,
			.wasDefeated = health->currentHitPoints <= 0.0f,
		};
	}


	void TickInvincibility(HealthComponent* health, float deltaTimeSeconds)
	{
		health->invincibleSecondsRemaining -= deltaTimeSeconds;
		if (health->invincibleSecondsRemaining <= INVINCIBLE_EPSILON_SECONDS)
		{
			health->invincibleSecondsRemaining = 0.0f;
		}
	}


	void SetMaximumHitPoints(HealthComponent* health, float maximumHitPoints)
	{
		const float newMaximum = maximumHitPoints > 0.0f ? maximumHitPoints : 0.0f;
		const float increase   = newMaximum - health->maximumHitPoints;

		health->maximumHitPoints = newMaximum;

		if (increase > 0.0f)
		{
			health->currentHitPoints += increase;
		}
		else if (health->currentHitPoints > newMaximum)
		{
			health->currentHitPoints = newMaximum;
		}
	}


	HealResult ApplyHeal(HealthComponent* health, float ratioOfMaximumHitPoints)
	{
		if (health->currentHitPoints >= health->maximumHitPoints)
		{
			return HealResult{};
		}

		const float healAmount = health->maximumHitPoints * std::max(ratioOfMaximumHitPoints, 0.0f);

		const float newCurrentHitPoints = std::min(health->currentHitPoints + healAmount, health->maximumHitPoints);
		const float healedHitPoints     = newCurrentHitPoints - health->currentHitPoints;

		health->currentHitPoints = newCurrentHitPoints;

		return HealResult{ .wasApplied = true, .healedHitPoints = healedHitPoints };
	}
} // namespace fang
