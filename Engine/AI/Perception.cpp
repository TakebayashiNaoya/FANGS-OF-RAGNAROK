/**
 * @file Perception.cpp
 * @brief 距離・視野角・遮蔽の 3 段で「見えているか」を決める、状態を持たない感知。
 */
#include "Pch.h"
#include "AI/Perception.h"
#include "Collision/CollisionWorld.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 長さの 2 乗をこれ以下とみなすと向きが決められない、という境目。 */
		constexpr float DEGENERATE_LENGTH_SQUARED = 1.0e-6f;
	} // namespace


	bool IsWithinSightCone(const PerceptionParameter& parameter, const PerceptionInput& input)
	{
		const Vector3 toTarget{
			input.targetPosition.x - input.selfPosition.x,
			0.0f,
			input.targetPosition.z - input.selfPosition.z,
		};

		const float distanceSquared = LengthSquared(toTarget);
		if (distanceSquared > parameter.sightRangeCentimeters * parameter.sightRangeCentimeters)
		{
			return false;
		}

		// 真上に重なっていて向きが決められないときは、視野角では落とさない（正面にいる扱い）。
		if (distanceSquared <= DEGENERATE_LENGTH_SQUARED)
		{
			return true;
		}

		const Vector3 facing{ std::cos(input.selfFacingRadians), 0.0f, std::sin(input.selfFacingRadians) };
		const float   cosAngle = Dot(facing, toTarget) / std::sqrt(distanceSquared);

		return cosAngle >= std::cos(parameter.halfFieldOfViewRadians);
	}


	PerceptionResult Sense(
		const CollisionWorld&      world,
		const PerceptionParameter& parameter,
		const PerceptionInput&     input
	)
	{
		PerceptionResult result;

		const Vector3 horizontalDelta{
			input.targetPosition.x - input.selfPosition.x,
			0.0f,
			input.targetPosition.z - input.selfPosition.z,
		};
		result.distanceCentimeters = Length(horizontalDelta);

		if (!IsWithinSightCone(parameter, input))
		{
			return result;
		}

		const Vector3 eyePosition{
			input.selfPosition.x,
			input.selfPosition.y + parameter.eyeHeightCentimeters,
			input.selfPosition.z,
		};
		const Vector3 targetEyePosition{
			input.targetPosition.x,
			input.targetPosition.y + parameter.targetEyeHeightCentimeters,
			input.targetPosition.z,
		};

		const uint32_t excluded[] = { input.selfUserIndex, input.targetUserIndex };

		const QueryFilter filter{
			.attributeMask       = parameter.blockerAttributeMask,
			.excludedUserIndices = excluded,
		};

		RaycastHit blockingHit;
		result.didTraceLineOfSight = true;
		result.isVisible           = world.HasLineOfSight(eyePosition, targetEyePosition, filter, &blockingHit);

		return result;
	}
} // namespace fang
