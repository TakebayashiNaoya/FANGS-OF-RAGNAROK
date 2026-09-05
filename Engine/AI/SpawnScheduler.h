/**
 * @file SpawnScheduler.h
 * @brief 一定の間隔で雑魚を 1 体ずつ、上限まで湧かせる係。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"
#include <cstdint>


namespace fang
{
	/** @brief 湧きの調整値。 */
	struct SpawnParams
	{
		FANG_REFLECT_BEGIN(SpawnParams)
		FANG_FIELD(intervalSeconds, "湧きの間隔", Range(0.0f, 60.0f))
		FANG_FIELD(minimumDistanceCentimeters, "最小距離", Range(0.0f, 10000.0f))
		FANG_FIELD(maximumDistanceCentimeters, "最大距離", Range(0.0f, 10000.0f))
		FANG_REFLECT_END()

		float intervalSeconds = 1.0f;

		/** @brief 同時上限。調整つまみではないので FANG_FIELD には出さない。 */
		uint32_t maximumAliveCount = 32;

		float minimumDistanceCentimeters = 2200.0f; /**< 索敵距離 2000 の外 ➡ 湧いた瞬間には見つからない。 */
		float maximumDistanceCentimeters = 3200.0f; /**< 地形の半径 4096 に収まる大きさ。 */
	};

	/** @brief このフレームの湧きの答え。 */
	struct SpawnRequest
	{
		bool    shouldSpawn = false;
		Vector3 position; /**< y は 0。地表へ載せるのは呼び出し側。 */
	};

	/**
	 * @brief 一定の間隔で 1 体ずつ、上限まで湧かせる係。
	 * @details 位置は相手を中心とした環の上を黄金角で回した点。乱数を持たないので、同じ順で同じ位置に出る。
	 * @threading 更新ジョブ 1 本から。
	 */
	class SpawnScheduler
	{
	public:
		[[nodiscard]] SpawnRequest Update(
			float              deltaTimeSeconds,
			uint32_t           aliveCount,
			const Vector3&     targetPosition,
			const SpawnParams& params
		);

		/** @brief 通算で湧かせた数。環の上の位置を決める番号でもある。 */
		[[nodiscard]] uint32_t GetSpawnedCount() const { return m_spawnedCount; }

		void Reset();


	private:
		float    m_elapsedSeconds = 0.0f;
		uint32_t m_spawnedCount   = 0;
	};
} // namespace fang
