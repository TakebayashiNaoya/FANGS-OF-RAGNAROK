/**
 * @file MinionSpawner.cpp
 * @brief 一定の間隔で雑魚を湧かせ、地表に立たせて Scene オブジェクトへ実体化する。
 */
#include "MinionSpawner.h"
#include "Resource/HeightmapTerrain.h"
#include "CollisionLayers.h"
#include "Minion.h"


namespace fang::game
{
	MinionSpawner::MinionSpawner()
	{
		// 置き物だけを遮蔽と数える(ADR-031)。雑魚どうしが遮り合うと密集した後列が永久に見失う。
		m_minionParams.perception.blockerLayerMask = COLLISION_LAYER_PROP;
	}


	void MinionSpawner::Update(float deltaTimeSeconds, const Vector3& targetPosition, const Dependencies& dependencies)
	{
		// 生きている数を毎フレーム数え直す。撃破された分の空きはここで自然に戻る（ADR-036）。
		// 通知を配る形にしないのは、撃破の経路が増えるたびに配り忘れが増えるため。
		uint32_t aliveCount = 0;
		for (uint32_t index = 0; index < m_aliveCount; ++index)
		{
			if (dependencies.scene->IsValid(m_spawnedHandles[index]))
			{
				m_spawnedHandles[aliveCount] = m_spawnedHandles[index];
				++aliveCount;
			}
		}
		m_aliveCount = aliveCount;

		const SpawnRequest request = m_scheduler.Update(deltaTimeSeconds, m_aliveCount, targetPosition, m_spawnParams);
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

		const GameObjectHandle handle = CreateMinionObject(
			*dependencies.scene,
			*dependencies.sharedModel,
			m_minionParams,
			dependencies.collisionWorld,
			dependencies.terrain,
			dependencies.targetHandle,
			spawnPosition
		);

		if (handle.IsValid() && m_aliveCount < MAX_TRACKED_MINION_COUNT)
		{
			m_spawnedHandles[m_aliveCount] = handle;
			++m_aliveCount;
		}
	}
} // namespace fang::game
