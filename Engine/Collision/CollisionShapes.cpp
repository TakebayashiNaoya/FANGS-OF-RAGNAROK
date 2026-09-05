/**
 * @file CollisionShapes.cpp
 * @brief 形を包む箱の計算と、描画用の箱からの形の導出。
 */
#include "Pch.h"
#include "Collision/CollisionShapes.h"
#include "Core/Math/Matrix4x4.h"
#include <cfloat>
#include <cmath>


namespace fang
{
	namespace
	{
		/**
		 * @brief OBB を包む軸平行の箱の、中心から各面までの距離。
		 * @details 3 軸を絶対値で足し合わせる。向きによらず必ず包む長さになるので、8 頂点を回して
		 *          最大最小を取り直さなくてよい（Aabb の TransformAabb と同じ理屈）。
		 */
		Vector3 ComputeOBBExtent(const OBB& box)
		{
			Vector3 extent;
			for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
			{
				const Vector3& axis       = box.axes[axisIndex];
				const float    halfExtent = GetComponent(box.halfExtents, axisIndex);

				extent.x += std::abs(axis.x) * halfExtent;
				extent.y += std::abs(axis.y) * halfExtent;
				extent.z += std::abs(axis.z) * halfExtent;
			}

			return extent;
		}
	} // namespace


	ColliderShape MakeColliderShape(const Sphere& sphere)
	{
		ColliderShape result;
		result.type   = EnShapeType::Sphere;
		result.sphere = sphere;
		return result;
	}


	ColliderShape MakeColliderShape(const Capsule& capsule)
	{
		ColliderShape result;
		result.type    = EnShapeType::Capsule;
		result.capsule = capsule;
		return result;
	}


	ColliderShape MakeColliderShape(const OBB& box)
	{
		ColliderShape result;
		result.type = EnShapeType::OBB;
		result.obb  = box;
		return result;
	}


	Aabb ComputeBounds(const ColliderShape& shape)
	{
		Aabb result;

		switch (shape.type)
		{
			case EnShapeType::Sphere:
			{
				const float   radius = shape.sphere.radius;
				const Vector3 extent{ radius, radius, radius };

				result.min = shape.sphere.center - extent;
				result.max = shape.sphere.center + extent;
				break;
			}

			case EnShapeType::Capsule:
			{
				const float   radius = shape.capsule.radius;
				const Vector3 extent{ radius, radius, radius };

				// 線分の両端を包んでから半径ぶん膨らませる。端の半球はこれで収まる。
				Aabb segmentBounds;
				segmentBounds.Expand(shape.capsule.pointA);
				segmentBounds.Expand(shape.capsule.pointB);

				result.min = segmentBounds.min - extent;
				result.max = segmentBounds.max + extent;
				break;
			}

			case EnShapeType::OBB:
			{
				const Vector3 extent = ComputeOBBExtent(shape.obb);

				result.min = shape.obb.center - extent;
				result.max = shape.obb.center + extent;
				break;
			}
		}

		return result;
	}


	OBB MakeOBBFromAabb(const Aabb& localBounds, const Matrix4x4& world)
	{
		FANG_ASSERT(localBounds.IsValid(), "無効な箱から OBB を作ろうとしている");

		const Vector3 localCenter      = (localBounds.min + localBounds.max) * 0.5f;
		const Vector3 localHalfExtents = (localBounds.max - localBounds.min) * 0.5f;

		OBB result;
		result.center = TransformPoint(localCenter, world);

		for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
		{
			const Vector3 row{ world.m[axisIndex][0], world.m[axisIndex][1], world.m[axisIndex][2] };
			const float   scale = Length(row);

			// 拡大率 0 の軸は向きを決められない。既定の単位軸を残し、長さだけ 0 にする
			// ➡ 潰れた行列でも Normalize のアサートに当たらない。
			if (scale > 0.0f)
			{
				result.axes[axisIndex] = row * (1.0f / scale);
			}

			SetComponent(&result.halfExtents, axisIndex, GetComponent(localHalfExtents, axisIndex) * scale);
		}

		return result;
	}


	Capsule MakeCapsuleFromAabb(const Aabb& localBounds, const Matrix4x4& world)
	{
		const OBB box = MakeOBBFromAabb(localBounds, world);

		int longestAxisIndex = 0;
		for (int axisIndex = 1; axisIndex < 3; ++axisIndex)
		{
			if (GetComponent(box.halfExtents, axisIndex) > GetComponent(box.halfExtents, longestAxisIndex))
			{
				longestAxisIndex = axisIndex;
			}
		}

		// 残り 2 軸の小さいほうを半径にする。大きいほうを採ると、細い向きに箱からはみ出す。
		float radius = FLT_MAX;
		for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
		{
			if (axisIndex != longestAxisIndex)
			{
				radius = std::fmin(radius, GetComponent(box.halfExtents, axisIndex));
			}
		}

		// 端の半球が箱からはみ出さないよう、線分の端を半径ぶん内側へ詰める。半径のほうが長ければ線分は潰れて球になる。
		const float longestHalfExtent = GetComponent(box.halfExtents, longestAxisIndex);
		const float segmentHalfLength = (longestHalfExtent > radius) ? (longestHalfExtent - radius) : 0.0f;

		const Vector3 offset = box.axes[longestAxisIndex] * segmentHalfLength;

		return Capsule{
			.pointA = box.center - offset,
			.pointB = box.center + offset,
			.radius = radius,
		};
	}
} // namespace fang
