/**
 * @file AgentBlackboard.h
 * @brief エージェント 1 体ぶんの局所の記憶と、感知結果の畳み込み。
 */
#pragma once

#include "AI/Perception.h"
#include "Core/Math/Vector3.h"


namespace fang
{
	/**
	 * @brief エージェント 1 体の局所の記憶。
	 * @details 群れで共有するブラックボードは、複数体の調整を入れるときに別に足す。
	 */
	struct AgentBlackboard
	{
		bool  isTargetVisible             = false;
		float distanceToTargetCentimeters = 0.0f;

		Vector3 lastSeenTargetPosition;
		bool    hasLastSeenPosition  = false;
		float   secondsSinceLastSeen = 0.0f; /**< 見えている間は 0。 */
	};

	/** @brief センサーの結果を記憶へ畳み込む。見えていれば位置を上書きし、見えていなければ秒数を進める。 */
	inline void WritePerception(
		const PerceptionResult& result,
		const Vector3&          targetPosition,
		float                   deltaTimeSeconds,
		AgentBlackboard*        blackboard
	)
	{
		blackboard->isTargetVisible             = result.isVisible;
		blackboard->distanceToTargetCentimeters = result.distanceCentimeters;

		if (result.isVisible)
		{
			blackboard->lastSeenTargetPosition = targetPosition;
			blackboard->hasLastSeenPosition    = true;
			blackboard->secondsSinceLastSeen   = 0.0f;
		}
		else
		{
			blackboard->secondsSinceLastSeen += deltaTimeSeconds;
		}
	}
} // namespace fang
