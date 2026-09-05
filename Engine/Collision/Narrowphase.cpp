/**
 * @file Narrowphase.cpp
 * @brief 形の組ごとの接触判定。最近点方式の 5 組と、OBB どうしの分離軸判定。
 */
#include "Pch.h"
#include "Collision/Narrowphase.h"
#include "Collision/CollisionMath.h"
#include <cfloat>
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 最近点が重なって向きを決められないときに使う押し出し方向。 */
		constexpr Vector3 FALLBACK_CONTACT_NORMAL{ 0.0f, 1.0f, 0.0f };

		/** @brief カプセルと OBB の最近点を詰める回数。浅いめり込みなら 2〜3 回で収まる。 */
		constexpr int CLOSEST_POINT_ITERATION_COUNT = 4;


		/**
		 * @brief 最近点の組から接触を組み立てる。
		 * @param closestOnA A 側の最近点（半径を足す前の芯の位置）。
		 * @param closestOnB B 側の最近点。
		 * @param radiusA    A の表面までの距離。
		 * @param radiusB    B の表面までの距離。
		 * @param fallbackNormal 最近点が重なって向きを決められないときの押し出し方向。
		 * @return 半径の和より近ければ true。
		 */
		bool MakeContactFromClosestPoints(
			const Vector3& closestOnA,
			const Vector3& closestOnB,
			float          radiusA,
			float          radiusB,
			const Vector3& fallbackNormal,
			Contact*       outContact
		)
		{
			const Vector3 delta           = closestOnB - closestOnA;
			const float   distanceSquared = LengthSquared(delta);
			const float   radiusSum       = radiusA + radiusB;

			if (distanceSquared > radiusSum * radiusSum)
			{
				return false;
			}

			// 最近点が重なると向きを決められない。呼び出し側が決めた向きを使う ➡ 長さ 0 を正規化しない。
			Vector3 normal   = fallbackNormal;
			float   distance = 0.0f;
			if (distanceSquared > DEGENERATE_LENGTH_SQUARED)
			{
				distance = std::sqrt(distanceSquared);
				normal   = delta * (1.0f / distance);
			}

			outContact->normal = normal;
			outContact->depth  = radiusSum - distance;
			outContact->point  = (closestOnA + normal * radiusA + closestOnB - normal * radiusB) * 0.5f;

			return true;
		}


		/**
		 * @brief 芯の点 + 半径と OBB の接触。球と OBB、カプセルと OBB の共通の出口。
		 * @details 分離距離と最近点は CollisionMath::ComputeCoreToBoxSeparation が持つ。ここでは半径との
		 *          比較と、芯 ➡ 箱の外向きから「1 つ目 ➡ 2 つ目」の向きへの反転、箱の外なら半径ぶん
		 *          割り引いた表面どうしの中間を接触点にする(2 つ目の半径 0 の MakeContactFromClosestPoints
		 *          と同じ式)だけを行う。
		 */
		bool IntersectCoreWithBox(const Vector3& center, float radius, const OBB& box, Contact* outContact)
		{
			const CoreBoxSeparation separation = ComputeCoreToBoxSeparation(center, box);
			if (separation.distance > radius)
			{
				return false;
			}

			outContact->normal = -separation.normal;
			outContact->depth  = radius - separation.distance;

			// 箱の中(芯が clamp で動かなかった)なら最近点がそのまま接触点。外なら芯側の表面まで縮める。
			outContact->point = (separation.distance > 0.0f)
									? (center - separation.normal * radius + separation.closestPoint) * 0.5f
									: separation.closestPoint;

			return true;
		}


		/** @brief 軸方向へ OBB を投影した半径。3 軸の寄与を絶対値で足す。 */
		float ProjectBoxRadius(const OBB& box, const Vector3& unitAxis)
		{
			float radius = 0.0f;
			for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
			{
				radius += std::abs(Dot(box.axes[axisIndex], unitAxis)) * GetComponent(box.halfExtents, axisIndex);
			}

			return radius;
		}


		/** @brief 向きへいちばん遠い頂点。接触点の近似に使う。 */
		Vector3 GetBoxSupportPoint(const OBB& box, const Vector3& direction)
		{
			Vector3 result = box.center;
			for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
			{
				const float halfExtent = GetComponent(box.halfExtents, axisIndex);
				const float sign       = (Dot(box.axes[axisIndex], direction) >= 0.0f) ? 1.0f : -1.0f;

				result += box.axes[axisIndex] * (halfExtent * sign);
			}

			return result;
		}


		/** @brief 分離軸の探索でいちばん浅い軸を控える。 */
		struct SeparatingAxisState
		{
			Vector3 axis;
			float   overlap     = FLT_MAX;
			bool    isSeparated = false;
		};


		/**
		 * @brief 1 本の軸へ両方の箱を投影し、分離していれば旗を立て、重なりが今までより浅ければ控える。
		 * @param axis 正規化していなくてよい。長さ 0 に近い軸（辺どうしが平行）は飛ばす。
		 */
		void TestSeparatingAxis(const Vector3& axis, const OBB& a, const OBB& b, SeparatingAxisState* state)
		{
			const float lengthSquared = LengthSquared(axis);

			// 辺どうしが平行だと外積が 0 になる。その向きの分離は面の軸 6 本が見ているので飛ばしてよい。
			if (lengthSquared <= DEGENERATE_LENGTH_SQUARED)
			{
				return;
			}

			const Vector3 unitAxis = axis * (1.0f / std::sqrt(lengthSquared));

			const float signedCenterDistance = Dot(b.center - a.center, unitAxis);
			const float overlap =
				ProjectBoxRadius(a, unitAxis) + ProjectBoxRadius(b, unitAxis) - std::abs(signedCenterDistance);

			if (overlap < 0.0f)
			{
				state->isSeparated = true;
				return;
			}

			if (overlap < state->overlap)
			{
				state->overlap = overlap;

				// 法線は必ず A から B へ向ける。
				state->axis = (signedCenterDistance >= 0.0f) ? unitAxis : -unitAxis;
			}
		}
	} // namespace


	bool Intersect(const Sphere& a, const Sphere& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		return MakeContactFromClosestPoints(
			a.center,
			b.center,
			a.radius,
			b.radius,
			FALLBACK_CONTACT_NORMAL,
			outContact
		);
	}


	bool Intersect(const Sphere& a, const Capsule& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		return MakeContactFromClosestPoints(
			a.center,
			ClosestPointOnSegment(b.pointA, b.pointB, a.center),
			a.radius,
			b.radius,
			FALLBACK_CONTACT_NORMAL,
			outContact
		);
	}


	bool Intersect(const Sphere& a, const OBB& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		return IntersectCoreWithBox(a.center, a.radius, b, outContact);
	}


	bool Intersect(const Capsule& a, const Capsule& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		Vector3 closestOnA;
		Vector3 closestOnB;
		ClosestPointsBetweenSegments(a.pointA, a.pointB, b.pointA, b.pointB, &closestOnA, &closestOnB);

		return MakeContactFromClosestPoints(
			closestOnA,
			closestOnB,
			a.radius,
			b.radius,
			FALLBACK_CONTACT_NORMAL,
			outContact
		);
	}


	bool Intersect(const Capsule& a, const OBB& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		// 線分を箱の軸に沿った座標へ移し、「箱へ clamp ➡ 線分へ投影し直す」で最近点を詰める。
		const Vector3 localStart = ToBoxLocal(b, a.pointA);
		const Vector3 localEnd   = ToBoxLocal(b, a.pointB);

		float parameter = 0.5f;
		for (int iteration = 0; iteration < CLOSEST_POINT_ITERATION_COUNT; ++iteration)
		{
			const Vector3 pointOnSegment = localStart + (localEnd - localStart) * parameter;
			parameter =
				ClosestParameterOnSegment(localStart, localEnd, ClampToHalfExtents(pointOnSegment, b.halfExtents));
		}

		// 芯が決まったら、あとは球と OBB と同じ経路で深さと法線を出す。
		return IntersectCoreWithBox(a.pointA + (a.pointB - a.pointA) * parameter, a.radius, b, outContact);
	}


	bool Intersect(const OBB& a, const OBB& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		SeparatingAxisState state;

		for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
		{
			TestSeparatingAxis(a.axes[axisIndex], a, b, &state);
			TestSeparatingAxis(b.axes[axisIndex], a, b, &state);
			if (state.isSeparated)
			{
				return false;
			}
		}

		for (int axisIndexA = 0; axisIndexA < 3; ++axisIndexA)
		{
			for (int axisIndexB = 0; axisIndexB < 3; ++axisIndexB)
			{
				TestSeparatingAxis(Cross(a.axes[axisIndexA], b.axes[axisIndexB]), a, b, &state);
				if (state.isSeparated)
				{
					return false;
				}
			}
		}

		// 軸が 1 本も試せなかった（3 軸とも長さ 0 の壊れた箱）。向きを決める材料が無いので触れていない扱いにする。
		if (state.overlap == FLT_MAX)
		{
			return false;
		}

		outContact->normal = state.axis;
		outContact->depth  = state.overlap;

		// 接触点は、その向きの支持点 2 つの中点。1 点の近似で、法線と深さは上で正確に出ている。
		outContact->point = (GetBoxSupportPoint(a, state.axis) + GetBoxSupportPoint(b, -state.axis)) * 0.5f;

		return true;
	}


	bool Intersect(const ColliderShape& a, const ColliderShape& b, Contact* outContact)
	{
		FANG_ASSERT(outContact != nullptr, "接触の書き込み先が null");

		switch (a.type)
		{
			case EnShapeType::Sphere:
				switch (b.type)
				{
					case EnShapeType::Sphere: return Intersect(a.sphere, b.sphere, outContact);
					case EnShapeType::Capsule: return Intersect(a.sphere, b.capsule, outContact);
					case EnShapeType::OBB: return Intersect(a.sphere, b.obb, outContact);
				}
				break;

			case EnShapeType::Capsule:
				switch (b.type)
				{
					case EnShapeType::Sphere:
						// 順を入れ替えて呼び、法線を 1 つ目 ➡ 2 つ目の向きへ戻す。
						if (!Intersect(b.sphere, a.capsule, outContact))
						{
							return false;
						}
						outContact->normal = -outContact->normal;
						return true;

					case EnShapeType::Capsule: return Intersect(a.capsule, b.capsule, outContact);
					case EnShapeType::OBB: return Intersect(a.capsule, b.obb, outContact);
				}
				break;

			case EnShapeType::OBB:
				switch (b.type)
				{
					case EnShapeType::Sphere:
						if (!Intersect(b.sphere, a.obb, outContact))
						{
							return false;
						}
						outContact->normal = -outContact->normal;
						return true;

					case EnShapeType::Capsule:
						if (!Intersect(b.capsule, a.obb, outContact))
						{
							return false;
						}
						outContact->normal = -outContact->normal;
						return true;

					case EnShapeType::OBB: return Intersect(a.obb, b.obb, outContact);
				}
				break;
		}

		return false;
	}
} // namespace fang
