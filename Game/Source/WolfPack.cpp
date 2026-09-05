/**
 * @file WolfPack.cpp
 * @brief 狼の席と、今どれを操作しているかを 1 か所で持つ入れ物。
 */
#include "WolfPack.h"
#include "GameLog.h"
#include "WolfBehavior.h"


namespace fang::game
{
	bool WolfPack::Add(GameObjectHandle handle, WolfBehavior* behavior)
	{
		if (m_count >= MAX_WOLF_COUNT)
		{
			FANG_LOG_WARNING(Game, "WolfPack の上限（{}）に達したので追加できなかった", MAX_WOLF_COUNT);
			return false;
		}

		m_handles[m_count]   = handle;
		m_behaviors[m_count] = behavior;
		++m_count;

		return true;
	}


	WolfPackUpdateResult WolfPack::Update(const Scene& scene)
	{
		// 1. 死んだ席を捨てる。解放済みのポインタが 1 フレームも残らないよう、誰かが触るより前に行う。
		//    並び順を保ったまま詰める(MinionSpawner の生存数え直しと同じ形)。
		uint32_t aliveSeatCount = 0;
		for (uint32_t index = 0; index < m_count; ++index)
		{
			if (scene.IsValid(m_handles[index]))
			{
				m_handles[aliveSeatCount]   = m_handles[index];
				m_behaviors[aliveSeatCount] = m_behaviors[index];
				++aliveSeatCount;
			}
		}
		m_count = aliveSeatCount;

		// 2. 操作対象を選び直す。席の並びがそのまま引き継ぎの順になる。
		const std::span<const GameObjectHandle> handles(m_handles.data(), m_count);
		const GameObjectHandle                  selectedHandle = FindFirstLiving(scene, handles);

		// 3. 選ばれた席が変わったら、その振る舞いへ伝える。
		if (selectedHandle != m_controlledHandle)
		{
			m_controlledHandle   = selectedHandle;
			m_controlledBehavior = nullptr;

			for (uint32_t index = 0; index < m_count; ++index)
			{
				if (m_handles[index] == selectedHandle)
				{
					m_controlledBehavior = m_behaviors[index];
					break;
				}
			}

			if (m_controlledBehavior != nullptr)
			{
				m_controlledBehavior->SetControlled(true);
			}
		}

		// 4. 全滅の立ち上がりを検知する。
		const uint32_t aliveCount = CountLiving(scene, handles);
		const bool     didWipeOut = aliveCount == 0 && !m_wasWipedOut;
		m_wasWipedOut             = aliveCount == 0;

		return WolfPackUpdateResult{ .aliveCount = aliveCount, .didWipeOut = didWipeOut };
	}
} // namespace fang::game
