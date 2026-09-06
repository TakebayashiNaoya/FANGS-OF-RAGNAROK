/**
 * @file MeatManager.cpp
 * @brief 場に出ている肉の席をまとめて持ち、寿命・回収・落下を進める係。
 */
#include "MeatManager.h"
#include "Meat.h"


namespace fang::game
{
	void MeatManager::Update(
		float                    deltaTimeSeconds,
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
		// 2. 回収(操作対象が生きているときだけ)。バッグが満杯になったらその場で打ち切る
		// 　(以降の席も同じ結果なので、肉は地面に残す)。
		//------------------------------------------------------------------------
		if (dependencies.collector != nullptr && dependencies.collector->IsValid())
		{
			const Vector3 collectorPosition = dependencies.collector->GetWorldPosition();

			for (uint32_t slotIndex = 0; slotIndex < MAX_MEAT_COUNT; ++slotIndex)
			{
				if (m_remainingSeconds[slotIndex] <= 0.0f)
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
		// 3. 落下。申し送りを先頭から消費し、1件ごとに通算番号を進める。
		//------------------------------------------------------------------------
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
			if (dependencies.scene != nullptr && dependencies.sharedModel != nullptr &&
				parameter.lifetimeSeconds > 0.0f)
			{
				meatActor = CreateMeatObject(*dependencies.scene, *dependencies.sharedModel, dropPosition);
			}

			m_actors[slotIndex]           = meatActor;
			m_positions[slotIndex]        = dropPosition;
			m_remainingSeconds[slotIndex] = meatActor.IsValid() ? parameter.lifetimeSeconds : 0.0f;
		}
		teamItems->pendingDropCount = 0;
	}
} // namespace fang::game
