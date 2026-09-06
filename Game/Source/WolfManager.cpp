/**
 * @file WolfManager.cpp
 * @brief 狼の席と、今どれを操作しているかを 1 か所で持つ入れ物。
 */
#include "WolfManager.h"
#include "Core/Reflection/TuningRegistry.h"
#include "Scene/ComponentTypes.h"
#include "GameLog.h"
#include "WolfController.h"


namespace fang::game
{
	bool WolfManager::Add(Actor actor, WolfController* controller)
	{
		if (m_count >= MAX_WOLF_COUNT)
		{
			FANG_LOG_WARNING(Game, "WolfManager の上限（{}）に達したので追加できなかった", MAX_WOLF_COUNT);
			return false;
		}

		m_actors[m_count]      = actor;
		m_controllers[m_count] = controller;
		++m_count;

		return true;
	}


	WolfManagerUpdateResult WolfManager::Update()
	{
		// 1. 死んだ席を捨てる。解放済みのポインタが 1 フレームも残らないよう、誰かが触るより前に行う。
		//    並び順を保ったまま詰める(EnemyManager の生存数え直しと同じ形)。
		uint32_t aliveSeatCount = 0;
		for (uint32_t index = 0; index < m_count; ++index)
		{
			if (m_actors[index].IsValid())
			{
				m_actors[aliveSeatCount]      = m_actors[index];
				m_controllers[aliveSeatCount] = m_controllers[index];
				++aliveSeatCount;
			}
		}
		m_count = aliveSeatCount;

		// 2. 撃破の申告を経験値へ変える。
		const int32_t defeatCount       = m_teamGrowth.pendingDefeatCount;
		m_teamGrowth.pendingDefeatCount = 0;

		const LevelGrowthResult growthResult = AddExperience(
			m_teamGrowth.levelGrowth,
			defeatCount * m_teamGrowth.experiencePerDefeat,
			&m_teamGrowth.levelProgress
		);

		// 3. 倍率を出す。狼はこの周の Update でこれを読む ➡ レベルは同じフレームのうちに攻撃力へ届く。
		m_teamGrowth.statusMultiplier =
			ComputeStatusMultiplier(m_teamGrowth.levelGrowth, m_teamGrowth.levelProgress.level);

		// 4. 最大 HP を全席へ配る。毎フレーム無条件 ➡ レベル・つまみ・席の増加のどれで変わったかを
		//    区別しない。増分だけ今の HP へ足すのは SetMaximumHitPoints の中。
		const float targetMaximumHitPoints = m_teamGrowth.baseMaximumHitPoints * m_teamGrowth.statusMultiplier;
		for (uint32_t index = 0; index < m_count; ++index)
		{
			if (HealthComponent* health = m_actors[index].GetHealthComponent(); health != nullptr)
			{
				SetMaximumHitPoints(health, targetMaximumHitPoints);
			}
		}

		// 5. 操作対象を選び直す。席の並びがそのまま引き継ぎの順になる。
		const std::span<const Actor> actors(m_actors.data(), m_count);
		const Actor*                 selectedActor = FindFirstLiving(actors);

		// 6. 選ばれた席が変わったら、その振る舞いへ伝える。
		const ActorHandle selectedHandle = (selectedActor != nullptr) ? selectedActor->GetHandle() : ActorHandle{};
		if (selectedHandle != m_controlledActor.GetHandle())
		{
			m_controlledActor = (selectedActor != nullptr) ? *selectedActor : Actor{};
			m_controlledWolf  = nullptr;

			for (uint32_t index = 0; index < m_count; ++index)
			{
				if (m_actors[index].GetHandle() == selectedHandle)
				{
					m_controlledWolf = m_controllers[index];
					break;
				}
			}

			if (m_controlledWolf != nullptr)
			{
				m_controlledWolf->SetControlled(true);
			}
		}

		// 7. 全滅の立ち上がりを検知する。
		const uint32_t aliveCount = CountLiving(actors);
		const bool     didWipeOut = aliveCount == 0 && !m_wasWipedOut;
		m_wasWipedOut             = aliveCount == 0;

		return WolfManagerUpdateResult{
			.aliveCount       = aliveCount,
			.didWipeOut       = didWipeOut,
			.gainedLevelCount = growthResult.gainedLevelCount,
		};
	}


	void WolfManager::RegisterTuningValues()
	{
		TuningRegistry& registry = TuningRegistry::GetInstance();
		FANG_VERIFY(registry.Register("狼の成長", &m_teamGrowth));
	}
} // namespace fang::game
