/**
 * @file Components.cpp
 * @brief HealthComponent のダメージ処理（ApplyDamage / TickInvincibility）。
 */
#include "Pch.h"
#include "Scene/Components.h"


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
} // namespace fang
