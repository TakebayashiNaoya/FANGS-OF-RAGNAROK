/**
 * @file EnemyManager.h
 * @brief 一定の間隔で雑魚を湧かせ、地表に立たせて Scene オブジェクトへ実体化する。
 */
#pragma once

#include "AI/AI.h"
#include "Core/Math/Vector3.h"
#include "Scene/Actor.h"
#include "EnemyController.h"
#include <array>


namespace fang
{
	class CollisionWorld;
	class HeightmapTerrain;
} // namespace fang


namespace fang::game
{
	struct WolfModel;

	/**
	 * @brief 間隔・上限・地表の高さを見て、雑魚を実際に湧かせる係。
	 * @details 「いつ・何体まで・どこに」の判断は AI::SpawnScheduler に任せ、ここは地表の高さを見て
	 *          実体化するかどうかだけを決める。地形の高さが取れない位置はそのフレームは見送る
	 *          （次の間隔で別の方位が出るので詰まらない）。
	 * @threading 更新ジョブ 1 本から。
	 */
	class EnemyManager
	{
	public:
		/** @brief 同時に追える雑魚の上限。SpawnParameter::maximumAliveCount はこれを超えられない。 */
		static constexpr uint32_t MAX_TRACKED_ENEMY_COUNT = 32;

		/** @brief Game 側が持ち続ける資源への借用。 */
		struct Dependencies
		{
			Scene*                  scene          = nullptr;
			WolfModel*              sharedModel    = nullptr; /**< 狼と共有するメッシュ・スキニング行列。 */
			CollisionWorld*         collisionWorld = nullptr;
			const HeightmapTerrain* terrain        = nullptr;

			/** @brief 追いかける相手（今の操作対象）。Game が持ち替えるので、寿命は呼び出し側が持つ。 */
			const Actor* target = nullptr;
		};

		/** @brief 1 フレームぶん進める。湧く条件が揃えば EnemyController を 1 体作る。 */
		void Update(float deltaTimeSeconds, const Vector3& targetPosition, const Dependencies& dependencies);

		/** @brief 今 Scene に生きている数。直近の Update が数え直したもの。上限判定に使う。 */
		[[nodiscard]] uint32_t GetAliveCount() const { return m_aliveCount; }


	private:
		SpawnScheduler m_scheduler;
		SpawnParameter m_spawnParameter;
		EnemyParameter m_enemyParameter;

		/** @brief 湧かせた雑魚。毎フレーム IsValid で数え直し、消えたものを詰める(ADR-036)。 */
		std::array<Actor, MAX_TRACKED_ENEMY_COUNT> m_spawnedActors;
		uint32_t                                   m_aliveCount = 0;
	};
} // namespace fang::game
