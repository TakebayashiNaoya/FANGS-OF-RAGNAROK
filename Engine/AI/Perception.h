/**
 * @file Perception.h
 * @brief 距離・視野角・遮蔽の 3 段で「見えているか」を決める、状態を持たない感知。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"
#include <cstdint>


namespace fang
{
	class CollisionWorld;

	/** @brief 1 体ぶんの感知の調整値。 */
	struct PerceptionParameter
	{
		FANG_REFLECT_BEGIN(PerceptionParameter)
		FANG_FIELD(sightRangeCentimeters, "索敵距離", Range(0.0f, 10000.0f))
		FANG_FIELD(halfFieldOfViewRadians, "視野角の半分", Range(0.0f, PI))
		FANG_FIELD(eyeHeightCentimeters, "目の高さ", Range(0.0f, 500.0f))
		FANG_FIELD(targetEyeHeightCentimeters, "相手の狙う高さ", Range(0.0f, 500.0f))
		FANG_REFLECT_END()

		float sightRangeCentimeters      = 2000.0f;
		float halfFieldOfViewRadians     = 1.0472f; /**< 60 度。視野は左右あわせて 120 度。 */
		float eyeHeightCentimeters       = 120.0f;
		float targetEyeHeightCentimeters = 120.0f;

		/** @brief 遮蔽と数える種別のビット。意味は Game が決める。調整つまみではないので反映しない。 */
		uint32_t blockerAttributeMask = ALL_COLLISION_ATTRIBUTE_MASK;
	};

	/** @brief 感知を呼ぶための、その瞬間の値。 */
	struct PerceptionInput
	{
		Vector3  selfPosition;             /**< 足元のワールド座標。 */
		float    selfFacingRadians = 0.0f; /**< 0 = +X。 */
		Vector3  targetPosition;           /**< 相手の足元。 */
		uint32_t selfUserIndex   = 0;
		uint32_t targetUserIndex = 0;
	};

	/** @brief 感知の答え。 */
	struct PerceptionResult
	{
		bool  isVisible           = false;
		bool  didTraceLineOfSight = false; /**< 視線を投げたか。1 フレームの本数を数えるため。 */
		float distanceCentimeters = 0.0f;  /**< 水平距離。見えていなくても入る。 */
	};

	/** @brief 距離と視野角だけの足切り。視線を投げる前にここで落とす。 */
	[[nodiscard]] bool IsWithinSightCone(const PerceptionParameter& parameter, const PerceptionInput& input);

	/**
	 * @brief 距離 ➡ 視野角 ➡ 遮蔽の順に見て、見えているかを答える。
	 * @details 視線は最大 1 本。手前の 2 つで落ちたら投げない。
	 *          filter には自分と相手の両方の userIndex を除外に入れる。
	 */
	[[nodiscard]] PerceptionResult Sense(
		const CollisionWorld&      world,
		const PerceptionParameter& parameter,
		const PerceptionInput&     input
	);
} // namespace fang
