/**
 * @file MeatManager.cpp
 * @brief 場に出ている肉の席をまとめて持ち、寿命・回収・落下・姿勢を進める係。
 */
#include "MeatManager.h"
#include "Meat.h"
#include "Stage.h"


namespace fang::game
{
	void MeatManager::Update(
		float                    deltaTimeSeconds,
		double                   elapsedSeconds,
		const ItemDropParameter& parameter,
		WolfTeamItems*           teamItems,
		const Dependencies&      dependencies
	)
	{
		//------------------------------------------------------------------------
		// 1. 寿命。尽きた席の Actor を破棄して空ける。ビットが立たなくても Actor が無効になっていれば
		// 　空ける(ADR-036 の数え直しと同じ守り)。
		//------------------------------------------------------------------------
		const uint32_t expiredSlotMask = StepItemLifetimes(m_remainingSeconds, deltaTimeSeconds);
		for (uint32_t slotIndex = 0; slotIndex < MAX_MEAT_COUNT; ++slotIndex)
		{
			const bool isExpired = (expiredSlotMask & (1u << slotIndex)) != 0;
			if (isExpired)
			{
				if (m_actors[slotIndex].IsValid())
				{
					m_actors[slotIndex].Destroy();
				}
				m_actors[slotIndex] = Actor{};
			}
			else if (m_remainingSeconds[slotIndex] > 0.0f && !m_actors[slotIndex].IsValid())
			{
				m_remainingSeconds[slotIndex] = 0.0f;
				m_actors[slotIndex]           = Actor{};
			}
		}

		//------------------------------------------------------------------------
		// 2. 回収(操作対象が生きているときだけ)。落ちてから拾えるまでの待ちを先に見て、明けていない席は
		// 　距離を見るまでもなく飛ばす。バッグが満杯になったらその場で打ち切る
		// 　(以降の席も同じ結果なので、肉は地面に残す)。
		//------------------------------------------------------------------------
		if (dependencies.collector != nullptr && dependencies.collector->IsValid())
		{
			const Vector3 collectorPosition = dependencies.collector->GetWorldPosition();

			for (uint32_t slotIndex = 0; slotIndex < MAX_MEAT_COUNT; ++slotIndex)
			{
				if (!IsItemReadyForPickup(parameter, m_remainingSeconds[slotIndex]))
				{
					continue;
				}

				if (!IsWithinPickupRange(parameter, m_positions[slotIndex], collectorPosition))
				{
					continue;
				}

				if (!AddItemToBag(parameter, &teamItems->bag))
				{
					break;
				}

				m_actors[slotIndex].Destroy();
				m_actors[slotIndex]           = Actor{};
				m_remainingSeconds[slotIndex] = 0.0f;
			}
		}

		//------------------------------------------------------------------------
		// 3. 落下。申し送りを先頭から消費し、1件ごとに通算番号を進める。行列はここでは書かない
		// 　(生成した周に姿勢の段と2回書くと FANG_ENABLE_SCENE_VALIDATION の重複検出に掛かる)。
		//------------------------------------------------------------------------
		const MeshId mesh   = dependencies.stage != nullptr ? dependencies.stage->placeholderMesh : MeshId{};
		const Aabb   bounds = dependencies.stage != nullptr ? dependencies.stage->placeholderLocalBounds : Aabb{};

		const uint32_t pendingDropCount = teamItems->pendingDropCount;
		for (uint32_t dropIndex = 0; dropIndex < pendingDropCount; ++dropIndex)
		{
			++m_defeatSerialNumber;
			if (!ShouldDropItem(parameter, m_defeatSerialNumber))
			{
				continue;
			}

			const Vector3  dropPosition = teamItems->pendingDropPositions[dropIndex];
			const uint32_t slotIndex    = SelectItemSlot(m_remainingSeconds);

			if (m_actors[slotIndex].IsValid())
			{
				m_actors[slotIndex].Destroy();
			}

			// 寿命0のつまみ位置では作らない。作ってしまうと「占有中なのに残り0」という、この配列全体が
			// 前提にしている不変条件(空き席=残り0)を壊した席が残り、二度と回収も入れ替えもされなくなる。
			Actor meatActor;
			if (dependencies.scene != nullptr && parameter.lifetimeSeconds > 0.0f)
			{
				meatActor = CreateMeatObject(*dependencies.scene, mesh, bounds);
			}

			m_actors[slotIndex]           = meatActor;
			m_positions[slotIndex]        = dropPosition;
			m_remainingSeconds[slotIndex] = meatActor.IsValid() ? parameter.lifetimeSeconds : 0.0f;
		}
		teamItems->pendingDropCount = 0;

		//------------------------------------------------------------------------
		// 4. 姿勢。全席の Transform を書く唯一の段(ADR-041)。落下の後に置くことで、この周に
		// 　生まれた席もこの周のうちに行列を持ち、Scene::Update がその周のワールド行列を組む。
		//------------------------------------------------------------------------
		for (uint32_t slotIndex = 0; slotIndex < MAX_MEAT_COUNT; ++slotIndex)
		{
			if (m_remainingSeconds[slotIndex] <= 0.0f || !m_actors[slotIndex].IsValid())
			{
				continue;
			}

			const Matrix4x4 displayMatrix = ComputeItemDisplayMatrix(parameter, m_positions[slotIndex], elapsedSeconds);
			(void)m_actors[slotIndex].SetLocalMatrix(displayMatrix);
		}
	}


	uint32_t MeatManager::GetActiveCount() const
	{
		uint32_t activeCount = 0;
		for (const float remainingSeconds : m_remainingSeconds)
		{
			if (remainingSeconds > 0.0f)
			{
				++activeCount;
			}
		}

		return activeCount;
	}
} // namespace fang::game
