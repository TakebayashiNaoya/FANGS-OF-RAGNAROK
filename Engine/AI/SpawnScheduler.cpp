/**
 * @file SpawnScheduler.cpp
 * @brief 一定の間隔で雑魚を 1 体ずつ、上限まで湧かせる係。
 */
#include "Pch.h"
#include "AI/SpawnScheduler.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 連続する 2 点が環の上で近くに並ばないようにする、方位の刻み（黄金角、ラジアン）。 */
		constexpr float GOLDEN_ANGLE_RADIANS = 2.39996f;

		/** @brief 半径を体数ごとにばらけさせる刻み（黄金比の共役）。 */
		constexpr float GOLDEN_RATIO_STEP = 0.61803f;
	} // namespace


	SpawnRequest SpawnScheduler::Update(
		float              deltaTimeSeconds,
		uint32_t           aliveCount,
		const Vector3&     targetPosition,
		const SpawnParams& params
	)
	{
		SpawnRequest request;

		if (aliveCount >= params.maximumAliveCount)
		{
			return request;
		}

		m_elapsedSeconds += deltaTimeSeconds;
		if (m_elapsedSeconds < params.intervalSeconds)
		{
			return request;
		}

		// 間隔を 1 回ぶんだけ引く ➡ フレームが飛んでも 1 フレームに 1 体しか湧かない。
		m_elapsedSeconds -= params.intervalSeconds;

		const float angleRadians = static_cast<float>(m_spawnedCount) * GOLDEN_ANGLE_RADIANS;

		const float radiusStep     = static_cast<float>(m_spawnedCount) * GOLDEN_RATIO_STEP;
		const float radiusFraction = radiusStep - std::floor(radiusStep);
		const float radius = params.minimumDistanceCentimeters +
							 (params.maximumDistanceCentimeters - params.minimumDistanceCentimeters) * radiusFraction;

		request.shouldSpawn = true;
		request.position    = Vector3{
			targetPosition.x + std::cos(angleRadians) * radius,
			0.0f,
			targetPosition.z + std::sin(angleRadians) * radius,
		};

		++m_spawnedCount;

		return request;
	}


	void SpawnScheduler::Reset()
	{
		m_elapsedSeconds = 0.0f;
		m_spawnedCount   = 0;
	}
} // namespace fang
