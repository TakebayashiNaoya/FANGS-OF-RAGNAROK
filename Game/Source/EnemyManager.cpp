/**
 * @file EnemyManager.cpp
 * @brief 一定の間隔で雑魚を湧かせ、地表に立たせて Scene オブジェクトへ実体化する。
 */
#include "EnemyManager.h"
#include "Resource/HeightmapTerrain.h"
#include "Enemy.h"


namespace fang::game
{
	void EnemyManager::Update(float deltaTimeSeconds, const Vector3& targetPosition, const Dependencies& dependencies)
	{
		// 生きている数を毎フレーム数え直す。撃破された分の空きはここで自然に戻る（ADR-036）。
		// 通知を配る形にしないのは、撃破の経路が増えるたびに配り忘れが増えるため。
		uint32_t aliveCount = 0;
		for (uint32_t index = 0; index < m_aliveCount; ++index)
		{
			if (m_spawnedActors[index].IsValid())
			{
				m_spawnedActors[aliveCount] = m_spawnedActors[index];
				++aliveCount;
			}
		}
		m_aliveCount = aliveCount;

		const SpawnRequest request =
			m_scheduler.Update(deltaTimeSeconds, m_aliveCount, targetPosition, m_spawnParameter);
		if (!request.shouldSpawn)
		{
			return;
		}

		float groundHeight = 0.0f;
		if (dependencies.terrain == nullptr ||
			!dependencies.terrain->TryGetHeightAt(request.position.x, request.position.z, &groundHeight))
		{
			// 地形の外、または地形が読めていない。この回は見送る（次の間隔で別の方位が出る）。
			return;
		}

		const Vector3 spawnPosition{ request.position.x, groundHeight, request.position.z };

		const CharacterCreateResult<EnemyController> result = CreateEnemyObject(
			*dependencies.scene,
			*dependencies.sharedModel,
			m_enemyParameter,
			dependencies.collisionWorld,
			dependencies.terrain,
			dependencies.target,
			spawnPosition
		);

		if (result.actor.IsValid() && m_aliveCount < MAX_TRACKED_ENEMY_COUNT)
		{
			m_spawnedActors[m_aliveCount] = result.actor;
			++m_aliveCount;
		}
	}
} // namespace fang::game
