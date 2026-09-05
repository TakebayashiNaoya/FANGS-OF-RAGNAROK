/**
 * @file PursuitStateMachine.cpp
 * @brief 待機 ➡ 追跡 ➡ 見失いの 3 状態で、相手へ近づく行き先を決める意思決定。
 */
#include "Pch.h"
#include "AI/PursuitStateMachine.h"
#include "AI/AgentBlackboard.h"
#include <algorithm>
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 長さの 2 乗をこれ以下とみなすと向きが決められない、という境目。 */
		constexpr float DEGENERATE_LENGTH_SQUARED = 1.0e-6f;

		/** @brief 距離そのものをこれ以下とみなすと進む向きが決められない、という境目。 */
		constexpr float DEGENERATE_DISTANCE_CENTIMETERS = 1.0e-3f;

		// AI は Scene に依存しない（設計判断）ので、Scene::GetYawFromDirection は使わず同じ式をここへ持つ。
		[[nodiscard]] float GetYawFromHorizontalDelta(const Vector3& delta)
		{
			if (delta.x * delta.x + delta.z * delta.z <= DEGENERATE_LENGTH_SQUARED)
			{
				return 0.0f;
			}

			return std::atan2(delta.z, delta.x);
		}
	} // namespace


	MoveIntent StepPursuit(
		const PursuitParams&   params,
		const AgentBlackboard& blackboard,
		const Vector3&         selfPosition,
		float                  deltaTimeSeconds,
		EnPursuitState*        state
	)
	{
		if (blackboard.isTargetVisible)
		{
			*state = EnPursuitState::Chase;
		}
		else if (*state == EnPursuitState::Chase)
		{
			*state = EnPursuitState::Search;
		}

		if (*state == EnPursuitState::Search && blackboard.secondsSinceLastSeen >= params.giveUpSeconds)
		{
			*state = EnPursuitState::Idle;
		}

		if (*state == EnPursuitState::Idle || !blackboard.hasLastSeenPosition)
		{
			return MoveIntent{};
		}

		const Vector3 horizontalDelta{
			blackboard.lastSeenTargetPosition.x - selfPosition.x,
			0.0f,
			blackboard.lastSeenTargetPosition.z - selfPosition.z,
		};
		const float distance = Length(horizontalDelta);

		if (*state == EnPursuitState::Search && distance <= params.arriveRadiusCentimeters)
		{
			*state = EnPursuitState::Idle;
			return MoveIntent{};
		}

		// 間合いでの停止はヒステリシスを使わない。進む量を残り距離で切り、0 以下なら動かない
		// ➡ 行き過ぎないので戻る動きが生まれず、押し合いにならない。
		const float stopDistance = (*state == EnPursuitState::Chase) ? params.stopDistanceCentimeters : 0.0f;
		const float remaining    = distance - stopDistance;

		MoveIntent intent;
		if (remaining > 0.0f && distance > DEGENERATE_DISTANCE_CENTIMETERS)
		{
			const float step = std::min(params.moveSpeedCentimetersPerSecond * deltaTimeSeconds, remaining);

			intent.desiredDelta  = horizontalDelta * (step / distance);
			intent.wantsToTurn   = true;
			intent.facingRadians = GetYawFromHorizontalDelta(horizontalDelta);
		}

		return intent;
	}
} // namespace fang
