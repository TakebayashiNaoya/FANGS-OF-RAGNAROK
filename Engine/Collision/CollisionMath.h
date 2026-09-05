/**
 * @file CollisionMath.h
 * @brief 判定とクエリで使い回す幾何の部品。
 * @details Collision の内部用。傘ヘッダ（Collision.h）には入れない。ここに置いてあるのは、Narrowphase と
 *          CollisionWorld の両方が要る小さな関数だけ。片方しか使わないものは、その .cpp の無名名前空間に置く。
 */
#pragma once

#include "Collision/CollisionShapes.h"
#include "Core/CoreMacros.h"
#include "Core/Math/Vector3.h"


namespace fang
{
	/**
	 * @brief 長さの 2 乗をこれ以下とみなすと向きが決められない、という境目。
	 * @details 位置の単位は 1 = 1cm なので、1e-6 は 1 マイクロメートルの 2 乗の桁。形の大きさに対して
	 *          十分小さく、float の丸めより十分大きい。
	 */
	inline constexpr float DEGENERATE_LENGTH_SQUARED = 1.0e-6f;

	/** @brief 長さでない量（向きの成分、2 次方程式の係数）をこれ以下なら 0 とみなす、という境目。 */
	inline constexpr float DEGENERATE_MAGNITUDE = 1.0e-6f;

	/** @brief 値を範囲へ収める。 */
	[[nodiscard]] FANG_FORCEINLINE float ClampFloat(float value, float minimum, float maximum)
	{
		return (value < minimum) ? minimum : ((value > maximum) ? maximum : value);
	}

	/** @brief 線分 pointA〜pointB のうち target に最も近い位置を 0〜1 で返す。線分が潰れていれば 0。 */
	[[nodiscard]] inline float ClosestParameterOnSegment(
		const Vector3& pointA,
		const Vector3& pointB,
		const Vector3& target
	)
	{
		const Vector3 segment       = pointB - pointA;
		const float   lengthSquared = LengthSquared(segment);
		if (lengthSquared <= DEGENERATE_LENGTH_SQUARED)
		{
			return 0.0f;
		}

		return ClampFloat(Dot(target - pointA, segment) / lengthSquared, 0.0f, 1.0f);
	}

	/** @brief 線分 pointA〜pointB のうち target に最も近い点。 */
	[[nodiscard]] inline Vector3 ClosestPointOnSegment(
		const Vector3& pointA,
		const Vector3& pointB,
		const Vector3& target
	)
	{
		return pointA + (pointB - pointA) * ClosestParameterOnSegment(pointA, pointB, target);
	}

	/** @brief ワールドの点を OBB の軸に沿った座標へ移す。 */
	[[nodiscard]] inline Vector3 ToBoxLocal(const OBB& box, const Vector3& worldPoint)
	{
		const Vector3 offset = worldPoint - box.center;

		return Vector3{ Dot(offset, box.axes[0]), Dot(offset, box.axes[1]), Dot(offset, box.axes[2]) };
	}

	/** @brief 向きを OBB の軸に沿った成分へ分ける。平行移動は掛からない。 */
	[[nodiscard]] inline Vector3 ToBoxLocalDirection(const OBB& box, const Vector3& worldDirection)
	{
		return Vector3{ Dot(worldDirection, box.axes[0]),
						Dot(worldDirection, box.axes[1]),
						Dot(worldDirection, box.axes[2]) };
	}

	/** @brief OBB の軸に沿った座標をワールドへ戻す。 */
	[[nodiscard]] inline Vector3 FromBoxLocal(const OBB& box, const Vector3& localPoint)
	{
		return box.center + box.axes[0] * localPoint.x + box.axes[1] * localPoint.y + box.axes[2] * localPoint.z;
	}

	/** @brief 軸に沿った座標を箱の中へ収める。 */
	[[nodiscard]] inline Vector3 ClampToHalfExtents(const Vector3& localPoint, const Vector3& halfExtents)
	{
		Vector3 result;
		for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
		{
			const float halfExtent = GetComponent(halfExtents, axisIndex);
			SetComponent(&result, axisIndex, ClampFloat(GetComponent(localPoint, axisIndex), -halfExtent, halfExtent));
		}

		return result;
	}
} // namespace fang
