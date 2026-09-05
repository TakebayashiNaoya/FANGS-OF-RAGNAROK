/**
 * @file CollisionMath.cpp
 * @brief 線分どうしの最近点と、芯の点と OBB の分離距離。
 */
#include "Pch.h"
#include "Collision/CollisionMath.h"
#include <cfloat>
#include <cmath>


namespace fang
{
	void ClosestPointsBetweenSegments(
		const Vector3& startA,
		const Vector3& endA,
		const Vector3& startB,
		const Vector3& endB,
		Vector3*       outOnA,
		Vector3*       outOnB
	)
	{
		const Vector3 directionA = endA - startA;
		const Vector3 directionB = endB - startB;
		const Vector3 offset     = startA - startB;

		const float lengthSquaredA = LengthSquared(directionA);
		const float lengthSquaredB = LengthSquared(directionB);
		const float projectionB    = Dot(directionB, offset);

		float parameterA = 0.0f;
		float parameterB = 0.0f;

		if (lengthSquaredA <= DEGENERATE_LENGTH_SQUARED && lengthSquaredB <= DEGENERATE_LENGTH_SQUARED)
		{
			// 両方とも点。始点どうしがそのまま最近点。
		}
		else if (lengthSquaredA <= DEGENERATE_LENGTH_SQUARED)
		{
			parameterB = ClampFloat(projectionB / lengthSquaredB, 0.0f, 1.0f);
		}
		else
		{
			const float projectionA = Dot(directionA, offset);

			if (lengthSquaredB <= DEGENERATE_LENGTH_SQUARED)
			{
				parameterA = ClampFloat(-projectionA / lengthSquaredA, 0.0f, 1.0f);
			}
			else
			{
				const float dotDirections = Dot(directionA, directionB);
				const float denominator   = lengthSquaredA * lengthSquaredB - dotDirections * dotDirections;

				parameterA = (denominator > DEGENERATE_LENGTH_SQUARED)
								 ? ClampFloat(
									   (dotDirections * projectionB - projectionA * lengthSquaredB) / denominator,
									   0.0f,
									   1.0f
								   )
								 : 0.0f;

				const float numeratorB = dotDirections * parameterA + projectionB;
				if (numeratorB < 0.0f)
				{
					parameterB = 0.0f;
					parameterA = ClampFloat(-projectionA / lengthSquaredA, 0.0f, 1.0f);
				}
				else if (numeratorB > lengthSquaredB)
				{
					parameterB = 1.0f;
					parameterA = ClampFloat((dotDirections - projectionA) / lengthSquaredA, 0.0f, 1.0f);
				}
				else
				{
					parameterB = numeratorB / lengthSquaredB;
				}
			}
		}

		*outOnA = startA + directionA * parameterA;
		*outOnB = startB + directionB * parameterB;
	}


	CoreBoxSeparation ComputeCoreToBoxSeparation(const Vector3& corePoint, const OBB& box)
	{
		const Vector3 local   = ToBoxLocal(box, corePoint);
		const Vector3 clamped = ClampToHalfExtents(local, box.halfExtents);

		const Vector3 difference = local - clamped;
		if (LengthSquared(difference) > DEGENERATE_LENGTH_SQUARED)
		{
			// 箱の外。最近点は面の上で、そこまでの距離をそのまま正の分離として返す。
			const Vector3 closestPoint = FromBoxLocal(box, clamped);
			const float   distance     = Length(difference);

			return CoreBoxSeparation{
				.closestPoint = closestPoint,
				.normal       = difference * (1.0f / distance),
				.distance     = distance,
			};
		}

		// 箱の中。最近点が芯そのものになって向きが決まらないので、いちばん近い面へ抜く向きを法線にする。
		int   shallowestAxisIndex = 0;
		float shallowestDistance  = FLT_MAX;
		float faceSign            = 1.0f;
		for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
		{
			const float coordinate     = GetComponent(local, axisIndex);
			const float distanceToFace = GetComponent(box.halfExtents, axisIndex) - std::abs(coordinate);

			if (distanceToFace < shallowestDistance)
			{
				shallowestDistance  = distanceToFace;
				shallowestAxisIndex = axisIndex;
				faceSign            = (coordinate >= 0.0f) ? 1.0f : -1.0f;
			}
		}

		const Vector3 faceNormal = box.axes[shallowestAxisIndex] * faceSign;

		return CoreBoxSeparation{
			.closestPoint = corePoint + faceNormal * shallowestDistance,
			.normal       = faceNormal,
			.distance     = -shallowestDistance,
		};
	}


	Vector3 ClosestPointOnSegmentToBox(const Vector3& segmentStart, const Vector3& segmentEnd, const OBB& box)
	{
		const Vector3 localStart = ToBoxLocal(box, segmentStart);
		const Vector3 localEnd   = ToBoxLocal(box, segmentEnd);

		float parameter = 0.5f;
		for (int iteration = 0; iteration < SEGMENT_TO_BOX_ITERATION_COUNT; ++iteration)
		{
			const Vector3 pointOnSegment = localStart + (localEnd - localStart) * parameter;
			parameter =
				ClosestParameterOnSegment(localStart, localEnd, ClampToHalfExtents(pointOnSegment, box.halfExtents));
		}

		return segmentStart + (segmentEnd - segmentStart) * parameter;
	}
} // namespace fang
