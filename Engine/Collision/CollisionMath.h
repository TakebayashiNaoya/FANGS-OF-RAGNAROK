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

	/** @brief 最近点が重なって向きを決められないときに使う押し出し方向。Narrowphase と掃引が共通で使う。 */
	inline constexpr Vector3 FALLBACK_CONTACT_NORMAL{ 0.0f, 1.0f, 0.0f };

	/** @brief 線分と OBB の最近点を詰める反復回数。浅いめり込みなら 2〜3 回で収まる。 */
	inline constexpr int SEGMENT_TO_BOX_ITERATION_COUNT = 4;

	/** @brief 値を範囲へ収める。 */
	[[nodiscard]] FANG_FORCEINLINE float ClampFloat(float value, float minimum, float maximum)
	{
		return (value < minimum) ? minimum : ((value > maximum) ? maximum : value);
	}

	/** @brief 2 つの箱が 3 軸すべてで重なっているか。境界が触れているだけでも重なりとする。 */
	[[nodiscard]] FANG_FORCEINLINE bool OverlapsOnAllAxes(const Aabb& left, const Aabb& right)
	{
		return left.min.x <= right.max.x && right.min.x <= left.max.x && left.min.y <= right.max.y &&
			   right.min.y <= left.max.y && left.min.z <= right.max.z && right.min.z <= left.max.z;
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

	/**
	 * @brief 2 つの線分の最近点の組を求める。
	 * @details 2 変数の 2 次関数の最小を解き、範囲外へ出た側を端で止めてもう一方を解き直す。平行で
	 *          分母が 0 になる場合は A 側を始点に固定する（平行なら A のどこを採っても距離は同じ）。
	 *          Narrowphase のカプセルどうしの判定と、掃引のカプセル・球の分離距離が共通で使う。
	 */
	void ClosestPointsBetweenSegments(
		const Vector3& startA,
		const Vector3& endA,
		const Vector3& startB,
		const Vector3& endB,
		Vector3*       outOnA,
		Vector3*       outOnB
	);

	/** @brief 芯の点（半径 0）と OBB の分離。 */
	struct CoreBoxSeparation
	{
		Vector3 closestPoint; /**< 芯に最も近い、箱の面上の点。芯が箱の中ならいちばん浅い面の上の点。 */
		Vector3 normal;       /**< 箱の外向き。芯が中にあればいちばん浅い面の外向き。 */

		/** @brief 芯から面までの符号付き距離。外なら正、中なら負（めり込みの深さの符号を反転したもの）。 */
		float distance = 0.0f;
	};

	/**
	 * @brief 芯の点と OBB の分離距離を求める。
	 * @details 芯が箱の外なら軸ごとの clamp で最近点が出る。中にあるときは最近点が芯そのものになって
	 *          向きも距離も決まらないので、いちばん近い面へ抜く向きを法線にして、面までの距離を負で返す。
	 *          Narrowphase の球・カプセルと OBB の判定と、掃引の OBB 相手の分離距離が共通で使う。
	 */
	[[nodiscard]] CoreBoxSeparation ComputeCoreToBoxSeparation(const Vector3& corePoint, const OBB& box);

	/**
	 * @brief 線分と OBB の最近点（芯）をワールド座標で返す。
	 * @details 「箱へ clamp ➡ 線分へ投影し直す」を SEGMENT_TO_BOX_ITERATION_COUNT 回繰り返す。
	 *          Narrowphase のカプセルと OBB の判定と、掃引の OBB 相手の分離距離が共通で使う。
	 */
	[[nodiscard]] Vector3 ClosestPointOnSegmentToBox(
		const Vector3& segmentStart,
		const Vector3& segmentEnd,
		const OBB&     box
	);
} // namespace fang
