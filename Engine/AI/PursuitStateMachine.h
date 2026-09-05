/**
 * @file PursuitStateMachine.h
 * @brief 待機 ➡ 追跡 ➡ 見失いの 3 状態で、相手へ近づく行き先を決める意思決定。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"
#include <cstdint>


namespace fang
{
	struct AgentBlackboard;

	/** @brief 追跡の状態。 */
	enum class EnPursuitState : uint8_t
	{
		Idle,   /**< 待機。その場から動かない。 */
		Chase,  /**< 追跡。見えている相手へ水平に近づく。 */
		Search, /**< 見失い。最後に見た位置まで進む。 */
	};

	/** @brief 1 体ぶんの追跡の調整値。 */
	struct PursuitParams
	{
		FANG_REFLECT_BEGIN(PursuitParams)
		FANG_FIELD(moveSpeedCentimetersPerSecond, "移動速度", Range(0.0f, 2000.0f))
		FANG_FIELD(turnSpeedRadiansPerSecond, "旋回速度", Range(0.0f, 32.0f))
		FANG_FIELD(stopDistanceCentimeters, "停止距離", Range(0.0f, 1000.0f))
		FANG_FIELD(arriveRadiusCentimeters, "到着半径", Range(0.0f, 1000.0f))
		FANG_FIELD(giveUpSeconds, "見失いの猶予秒数", Range(0.0f, 60.0f))
		FANG_REFLECT_END()

		float moveSpeedCentimetersPerSecond = 350.0f; /**< 狼の 400 より遅い ➡ 直線では振り切れる。 */
		float turnSpeedRadiansPerSecond     = 6.0f;
		float stopDistanceCentimeters       = 200.0f; /**< これより近づかない。 */
		float arriveRadiusCentimeters       = 120.0f; /**< 最後に見た位置へ着いたと見なす半径。 */
		float giveUpSeconds                 = 5.0f;   /**< 見失ってから待機へ戻るまで。 */
	};

	/** @brief このフレームに進みたい量と向きたい向き。 */
	struct MoveIntent
	{
		Vector3 desiredDelta; /**< 水平。動かないときは 0。 */
		float   facingRadians = 0.0f;
		bool    wantsToTurn   = false; /**< false なら向きを触らない。 */
	};

	/**
	 * @brief 状態を 1 フレーム進め、行き先を返す。
	 * @param state 呼び出し側が持つ状態。この関数が書き換える。
	 */
	[[nodiscard]] MoveIntent StepPursuit(
		const PursuitParams&   params,
		const AgentBlackboard& blackboard,
		const Vector3&         selfPosition,
		float                  deltaTimeSeconds,
		EnPursuitState*        state
	);
} // namespace fang
