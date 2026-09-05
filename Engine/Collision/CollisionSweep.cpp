/**
 * @file CollisionSweep.cpp
 * @brief 保守的前進による掃引。
 */
#include "Pch.h"
#include "Collision/CollisionSweep.h"
#include "Collision/CollisionMath.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/**
		 * @brief 芯どうしの最近点 2 つから分離を組み立てる。Narrowphase の MakeContactFromClosestPoints と
		 *        同じ式で、深さでなく符号付きの分離距離を返す。
		 */
		void ComputeSeparationFromPoints(
			const Vector3& pointOnA,
			const Vector3& pointOnB,
			float          radiusA,
			float          radiusB,
			Separation*    outSeparation
		)
		{
			const Vector3 delta           = pointOnB - pointOnA;
			const float   distanceSquared = LengthSquared(delta);

			Vector3 normal         = FALLBACK_CONTACT_NORMAL;
			float   centerDistance = 0.0f;
			if (distanceSquared > DEGENERATE_LENGTH_SQUARED)
			{
				centerDistance = std::sqrt(distanceSquared);
				normal         = delta * (1.0f / centerDistance);
			}

			outSeparation->pointOnA = pointOnA;
			outSeparation->normal   = normal;
			outSeparation->distance = centerDistance - (radiusA + radiusB);
		}
	} // namespace


	void ComputeSeparation(const Capsule& movingShape, const ColliderShape& target, Separation* outSeparation)
	{
		FANG_ASSERT(outSeparation != nullptr, "分離の書き込み先が null");

		switch (target.type)
		{
			case EnShapeType::Sphere:
			{
				const Vector3 pointOnA =
					ClosestPointOnSegment(movingShape.pointA, movingShape.pointB, target.sphere.center);

				ComputeSeparationFromPoints(
					pointOnA,
					target.sphere.center,
					movingShape.radius,
					target.sphere.radius,
					outSeparation
				);
				return;
			}

			case EnShapeType::Capsule:
			{
				Vector3 pointOnA;
				Vector3 pointOnB;
				ClosestPointsBetweenSegments(
					movingShape.pointA,
					movingShape.pointB,
					target.capsule.pointA,
					target.capsule.pointB,
					&pointOnA,
					&pointOnB
				);

				ComputeSeparationFromPoints(
					pointOnA,
					pointOnB,
					movingShape.radius,
					target.capsule.radius,
					outSeparation
				);
				return;
			}

			case EnShapeType::OBB:
			{
				const Vector3 pointOnA = ClosestPointOnSegmentToBox(movingShape.pointA, movingShape.pointB, target.obb);
				const CoreBoxSeparation boxSeparation = ComputeCoreToBoxSeparation(pointOnA, target.obb);

				outSeparation->pointOnA = pointOnA;

				// CoreBoxSeparation::normal は箱の外向き(B ➡ A)。Separation::normal は A ➡ B なので反転する。
				outSeparation->normal   = -boxSeparation.normal;
				outSeparation->distance = boxSeparation.distance - movingShape.radius;
				return;
			}
		}
	}


	bool SweepAgainstShape(
		const Capsule&       movingShape,
		const Vector3&       motion,
		const ColliderShape& target,
		SweepHit*            outHit
	)
	{
		FANG_ASSERT(outHit != nullptr, "掃引ヒットの書き込み先が null");

		float timeRatio = 0.0f;

		for (int iteration = 0; iteration < SWEEP_ITERATION_COUNT; ++iteration)
		{
			const Vector3 offset = motion * timeRatio;
			const Capsule advanced{
				.pointA = movingShape.pointA + offset,
				.pointB = movingShape.pointB + offset,
				.radius = movingShape.radius,
			};

			Separation separation;
			ComputeSeparation(advanced, target, &separation);

			if (separation.distance <= SWEEP_TOUCH_TOLERANCE)
			{
				// 触れた瞬間の movingShape 側の表面点。Separation::normal は A(movingShape) ➡ B(target) の向き。
				outHit->point     = separation.pointOnA + separation.normal * movingShape.radius;
				outHit->normal    = -separation.normal;
				outHit->timeRatio = timeRatio;
				return true;
			}

			const float closingSpeed = Dot(motion, separation.normal);
			if (closingSpeed <= 0.0f)
			{
				// 離れていく向き。これ以上進めても近づかない。
				return false;
			}

			timeRatio += separation.distance / closingSpeed;
			if (timeRatio > 1.0f)
			{
				return false;
			}
		}

		// 16 回で許容差まで詰まらなかった。ほぼ接線方向にかすめる配置で、値が信用できないので当たらない扱いにする。
		return false;
	}
} // namespace fang
