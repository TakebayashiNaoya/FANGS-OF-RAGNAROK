/**
 * @file CollisionWorld.cpp
 * @brief コライダーの登録と接触の作り直し、レイキャストと球の重なり。
 */
#include "Pch.h"
#include "Collision/CollisionWorld.h"
#include "Collision/CollisionLog.h"
#include "Collision/CollisionMath.h"
#include "Collision/CollisionSweep.h"
#include "Core/Memory/Allocator.h"
#include <cfloat>
#include <cmath>


namespace fang
{
	namespace
	{
		/**
		 * @brief 視線が対象の手前で止まる余白。1 = 1cm なので 0.1mm。
		 * @details 対象自身の登録がちょうど到達点にあっても、それを遮蔽として拾わないための余白。
		 */
		constexpr float LINE_OF_SIGHT_TARGET_MARGIN = 0.01f;


		/**
		 * @brief レイが箱と交わるか。形ごとの厳密判定の前に候補を落とすために使う。
		 * @details スラブ法。向きが軸に平行な成分は 0 除算になるので、始点が範囲外かどうかだけで決める。
		 */
		bool IntersectsRayWithAabb(
			const Aabb&    bounds,
			const Vector3& origin,
			const Vector3& direction,
			float          maxDistance
		)
		{
			float entryDistance = 0.0f;
			float exitDistance  = maxDistance;

			for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
			{
				const float originOnAxis    = GetComponent(origin, axisIndex);
				const float directionOnAxis = GetComponent(direction, axisIndex);
				const float minimumOnAxis   = GetComponent(bounds.min, axisIndex);
				const float maximumOnAxis   = GetComponent(bounds.max, axisIndex);

				if (std::abs(directionOnAxis) <= DEGENERATE_MAGNITUDE)
				{
					// この軸には進まない。始点が範囲の外なら、どこまで行っても入らない。
					if (originOnAxis < minimumOnAxis || originOnAxis > maximumOnAxis)
					{
						return false;
					}

					continue;
				}

				const float inverseDirection = 1.0f / directionOnAxis;

				float nearDistance = (minimumOnAxis - originOnAxis) * inverseDirection;
				float farDistance  = (maximumOnAxis - originOnAxis) * inverseDirection;
				if (nearDistance > farDistance)
				{
					const float swapped = nearDistance;
					nearDistance        = farDistance;
					farDistance         = swapped;
				}

				entryDistance = (nearDistance > entryDistance) ? nearDistance : entryDistance;
				exitDistance  = (farDistance < exitDistance) ? farDistance : exitDistance;

				if (entryDistance > exitDistance)
				{
					return false;
				}
			}

			return true;
		}


		/**
		 * @brief レイと球。
		 * @return 当たれば true。始点が球の中なら距離 0、法線は -direction。
		 */
		bool RaycastSphere(
			const Sphere&  sphere,
			const Vector3& origin,
			const Vector3& direction,
			float          maxDistance,
			float*         outDistance,
			Vector3*       outNormal
		)
		{
			const Vector3 toCenter      = sphere.center - origin;
			const float   radiusSquared = sphere.radius * sphere.radius;

			if (LengthSquared(toCenter) <= radiusSquared)
			{
				*outDistance = 0.0f;
				*outNormal   = -direction;
				return true;
			}

			const float projection = Dot(toCenter, direction);
			if (projection < 0.0f)
			{
				// 球は始点の後ろにある。
				return false;
			}

			const float closestDistanceSquared = LengthSquared(toCenter) - projection * projection;
			if (closestDistanceSquared > radiusSquared)
			{
				return false;
			}

			const float distance = projection - std::sqrt(radiusSquared - closestDistanceSquared);
			if (distance > maxDistance)
			{
				return false;
			}

			*outDistance = distance;
			*outNormal   = Normalize(origin + direction * distance - sphere.center);

			return true;
		}


		/** @brief レイと OBB。箱の軸に沿った座標へ移してスラブ法で解く。 */
		bool RaycastBox(
			const OBB&     box,
			const Vector3& origin,
			const Vector3& direction,
			float          maxDistance,
			float*         outDistance,
			Vector3*       outNormal
		)
		{
			const Vector3 localOrigin    = ToBoxLocal(box, origin);
			const Vector3 localDirection = ToBoxLocalDirection(box, direction);

			float entryDistance = 0.0f;
			float exitDistance  = maxDistance;

			int   entryAxisIndex = -1;
			float entryFaceSign  = 1.0f;

			for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
			{
				const float halfExtent      = GetComponent(box.halfExtents, axisIndex);
				const float originOnAxis    = GetComponent(localOrigin, axisIndex);
				const float directionOnAxis = GetComponent(localDirection, axisIndex);

				if (std::abs(directionOnAxis) <= DEGENERATE_MAGNITUDE)
				{
					if (originOnAxis < -halfExtent || originOnAxis > halfExtent)
					{
						return false;
					}

					continue;
				}

				const float inverseDirection = 1.0f / directionOnAxis;

				float nearDistance = (-halfExtent - originOnAxis) * inverseDirection;
				float farDistance  = (halfExtent - originOnAxis) * inverseDirection;

				// 進む向きが正なら -half の面から、負なら +half の面から入る。
				float faceSign = -1.0f;
				if (nearDistance > farDistance)
				{
					const float swapped = nearDistance;
					nearDistance        = farDistance;
					farDistance         = swapped;
					faceSign            = 1.0f;
				}

				if (nearDistance > entryDistance)
				{
					entryDistance  = nearDistance;
					entryAxisIndex = axisIndex;
					entryFaceSign  = faceSign;
				}

				exitDistance = (farDistance < exitDistance) ? farDistance : exitDistance;

				if (entryDistance > exitDistance)
				{
					return false;
				}
			}

			*outDistance = entryDistance;

			// どの面も跨がずに中にいた ➡ 始点が箱の中。
			*outNormal = (entryAxisIndex < 0) ? -direction : box.axes[entryAxisIndex] * entryFaceSign;

			return true;
		}


		/**
		 * @brief レイとカプセル。
		 * @details 無限円柱の 2 次方程式を解いて中心線の範囲に入っていれば採り、端の 2 つの半球とも比べて
		 *          いちばん近いものを返す。潰れたカプセルは球として解く。
		 */
		bool RaycastCapsule(
			const Capsule& capsule,
			const Vector3& origin,
			const Vector3& direction,
			float          maxDistance,
			float*         outDistance,
			Vector3*       outNormal
		)
		{
			const Vector3 axis              = capsule.pointB - capsule.pointA;
			const float   axisLengthSquared = LengthSquared(axis);

			if (axisLengthSquared <= DEGENERATE_LENGTH_SQUARED)
			{
				return RaycastSphere(
					Sphere{ .center = capsule.pointA, .radius = capsule.radius },
					origin,
					direction,
					maxDistance,
					outDistance,
					outNormal
				);
			}

			// 始点が中なら距離 0。円柱の式より先に見る（中だと 2 次方程式の根が後ろに出る）。
			const Vector3 closestOnAxis = ClosestPointOnSegment(capsule.pointA, capsule.pointB, origin);
			if (LengthSquared(origin - closestOnAxis) <= capsule.radius * capsule.radius)
			{
				*outDistance = 0.0f;
				*outNormal   = -direction;
				return true;
			}

			float   bestDistance = FLT_MAX;
			Vector3 bestNormal;

			// 無限円柱。a t^2 + 2b t + c = 0 で、軸に沿った成分を抜いた距離が半径に等しくなる t を解く。
			const Vector3 offset          = origin - capsule.pointA;
			const float   directionOnAxis = Dot(direction, axis);
			const float   offsetOnAxis    = Dot(offset, axis);
			const float   quadraticA      = 1.0f - directionOnAxis * directionOnAxis / axisLengthSquared;
			const float   quadraticHalfB  = Dot(direction, offset) - directionOnAxis * offsetOnAxis / axisLengthSquared;
			const float   quadraticC      = LengthSquared(offset) - offsetOnAxis * offsetOnAxis / axisLengthSquared -
											capsule.radius * capsule.radius;

			// a が 0 に近いのはレイが軸と平行なとき。そのときは端の半球だけで決まる。
			if (std::abs(quadraticA) > DEGENERATE_MAGNITUDE)
			{
				const float discriminant = quadraticHalfB * quadraticHalfB - quadraticA * quadraticC;
				if (discriminant >= 0.0f)
				{
					const float distance = (-quadraticHalfB - std::sqrt(discriminant)) / quadraticA;
					if (distance >= 0.0f && distance <= maxDistance)
					{
						const Vector3 hitPoint      = origin + direction * distance;
						const float   axisParameter = Dot(hitPoint - capsule.pointA, axis) / axisLengthSquared;

						// 側面に当たったのは、中心線の範囲に収まっているときだけ。外なら端の半球の担当。
						if (axisParameter >= 0.0f && axisParameter <= 1.0f)
						{
							// 半径 0 のカプセルは軸の上で当たるので、外向きが決められない。
							const Vector3 outward = hitPoint - (capsule.pointA + axis * axisParameter);

							bestDistance = distance;
							bestNormal =
								(LengthSquared(outward) > DEGENERATE_LENGTH_SQUARED) ? Normalize(outward) : -direction;
						}
					}
				}
			}

			const Vector3 endPoints[2] = { capsule.pointA, capsule.pointB };
			for (const Vector3& endPoint : endPoints)
			{
				float   endDistance = 0.0f;
				Vector3 endNormal;
				if (RaycastSphere(
						Sphere{ .center = endPoint, .radius = capsule.radius },
						origin,
						direction,
						maxDistance,
						&endDistance,
						&endNormal
					) &&
					endDistance < bestDistance)
				{
					bestDistance = endDistance;
					bestNormal   = endNormal;
				}
			}

			if (bestDistance == FLT_MAX)
			{
				return false;
			}

			*outDistance = bestDistance;
			*outNormal   = bestNormal;

			return true;
		}


		/** @brief 登録がクエリの絞り込みを通るか。 */
		bool PassesFilter(const ColliderProxy& proxy, const QueryFilter& filter)
		{
			if ((proxy.attributeMask & filter.attributeMask) == 0)
			{
				return false;
			}

			for (const uint32_t excludedUserIndex : filter.excludedUserIndices)
			{
				if (excludedUserIndex == proxy.userIndex)
				{
					return false;
				}
			}

			return true;
		}


		/** @brief 形の種類で振り分けるレイキャスト。 */
		bool RaycastShape(
			const ColliderShape& shape,
			const Vector3&       origin,
			const Vector3&       direction,
			float                maxDistance,
			float*               outDistance,
			Vector3*             outNormal
		)
		{
			switch (shape.type)
			{
				case EnShapeType::Sphere:
					return RaycastSphere(shape.sphere, origin, direction, maxDistance, outDistance, outNormal);
				case EnShapeType::Capsule:
					return RaycastCapsule(shape.capsule, origin, direction, maxDistance, outDistance, outNormal);
				case EnShapeType::OBB:
					return RaycastBox(shape.obb, origin, direction, maxDistance, outDistance, outNormal);
			}

			return false;
		}


		/**
		 * @brief 掃引の結果を近い順に挿入する。満杯なら遠いほうを比べ、押し出せなければ捨てる。
		 * @details どちらの経路でも、書き込めなかったら isTruncated を立てる。
		 */
		void InsertSweepHitByDistance(const SweepHit& hit, std::span<SweepHit> outHits, SweepResult* result)
		{
			if (result->hitCount < outHits.size())
			{
				uint32_t insertPosition = result->hitCount;
				while (insertPosition > 0 && outHits[insertPosition - 1].timeRatio > hit.timeRatio)
				{
					outHits[insertPosition] = outHits[insertPosition - 1];
					--insertPosition;
				}

				outHits[insertPosition] = hit;
				++result->hitCount;
				return;
			}

			if (!outHits.empty() && hit.timeRatio < outHits.back().timeRatio)
			{
				uint32_t insertPosition = static_cast<uint32_t>(outHits.size()) - 1;
				while (insertPosition > 0 && outHits[insertPosition - 1].timeRatio > hit.timeRatio)
				{
					outHits[insertPosition] = outHits[insertPosition - 1];
					--insertPosition;
				}

				outHits[insertPosition] = hit;
			}

			result->isTruncated = true;
		}
	} // namespace


	CollisionWorld::~CollisionWorld()
	{
		Shutdown();
	}


	std::span<const Contact> CollisionWorld::GetContacts() const
	{
		return std::span<const Contact>(m_contacts, m_contactCount);
	}


	bool CollisionWorld::Initialize(IAllocator& allocator, const CollisionWorldDesc& desc)
	{
		FANG_ASSERT(m_allocator == nullptr, "二重に初期化しようとしている");

		if (desc.maxColliderCount == 0 || desc.maxPairCount == 0 || desc.maxContactCount == 0)
		{
			FANG_LOG_ERROR(
				Collision,
				"CollisionWorld の上限に 0 が混じっている: コライダー {} / 組 {} / 接触 {}",
				desc.maxColliderCount,
				desc.maxPairCount,
				desc.maxContactCount
			);
			return false;
		}

		m_proxies    = NewArray<ColliderProxy>(allocator, desc.maxColliderCount);
		m_bounds     = NewArray<Aabb>(allocator, desc.maxColliderCount);
		m_pairs      = NewArray<ColliderPair>(allocator, desc.maxPairCount);
		m_contacts   = NewArray<Contact>(allocator, desc.maxContactCount);
		m_broadphase = CreateBroadphase(allocator, desc.broadphaseType);

		const bool hasAllBuffers = m_proxies != nullptr && m_bounds != nullptr && m_pairs != nullptr &&
								   m_contacts != nullptr && m_broadphase != nullptr;
		if (!hasAllBuffers || !m_broadphase->Initialize(allocator, desc.maxColliderCount))
		{
			FANG_LOG_ERROR(Collision, "CollisionWorld の置き場を確保できなかった");

			// 途中まで取れていたぶんを返す。ここで確保した数がまだメンバに入っていないので desc から数える。
			DeleteArray(allocator, m_contacts, desc.maxContactCount);
			DeleteArray(allocator, m_pairs, desc.maxPairCount);
			DeleteArray(allocator, m_bounds, desc.maxColliderCount);
			DeleteArray(allocator, m_proxies, desc.maxColliderCount);
			if (m_broadphase != nullptr)
			{
				m_broadphase->Shutdown();
			}
			DestroyBroadphase(allocator, m_broadphase);

			m_contacts   = nullptr;
			m_pairs      = nullptr;
			m_bounds     = nullptr;
			m_proxies    = nullptr;
			m_broadphase = nullptr;
			return false;
		}

		m_allocator        = &allocator;
		m_maxColliderCount = desc.maxColliderCount;
		m_maxPairCount     = desc.maxPairCount;
		m_maxContactCount  = desc.maxContactCount;
		m_colliderCount    = 0;
		m_contactCount     = 0;

		FANG_LOG_INFO(
			Collision,
			"CollisionWorld を作った: コライダー {} / 組 {} / 接触 {} / Broadphase {}",
			m_maxColliderCount,
			m_maxPairCount,
			m_maxContactCount,
			GetBroadphaseName()
		);

		return true;
	}


	void CollisionWorld::Shutdown()
	{
		if (m_allocator == nullptr)
		{
			return;
		}

		m_broadphase->Shutdown();
		DestroyBroadphase(*m_allocator, m_broadphase);

		DeleteArray(*m_allocator, m_contacts, m_maxContactCount);
		DeleteArray(*m_allocator, m_pairs, m_maxPairCount);
		DeleteArray(*m_allocator, m_bounds, m_maxColliderCount);
		DeleteArray(*m_allocator, m_proxies, m_maxColliderCount);

		m_contacts   = nullptr;
		m_pairs      = nullptr;
		m_bounds     = nullptr;
		m_proxies    = nullptr;
		m_broadphase = nullptr;

		m_allocator        = nullptr;
		m_maxColliderCount = 0;
		m_maxPairCount     = 0;
		m_maxContactCount  = 0;
		m_colliderCount    = 0;
		m_contactCount     = 0;
	}


	void CollisionWorld::Update(std::span<const ColliderProxy> proxies)
	{
		FANG_ASSERT(m_allocator != nullptr, "初期化していない CollisionWorld を更新しようとしている");

		const uint32_t requestedCount = static_cast<uint32_t>(proxies.size());

		m_colliderCount = (requestedCount < m_maxColliderCount) ? requestedCount : m_maxColliderCount;
		if (requestedCount > m_colliderCount)
		{
			FANG_LOG_WARNING(
				Collision,
				"コライダーの上限を超えた分を捨てた: {} 個中 {} 個",
				requestedCount,
				m_colliderCount
			);
		}

		for (uint32_t index = 0; index < m_colliderCount; ++index)
		{
			m_proxies[index] = proxies[index];
			m_bounds[index]  = ComputeBounds(m_proxies[index].shape);
		}

		IBroadphase& broadphase = GetBroadphase();
		broadphase.Build(std::span<const Aabb>(m_bounds, m_colliderCount));

		const uint32_t pairCount = broadphase.CollectPairs(std::span<ColliderPair>(m_pairs, m_maxPairCount));

		m_contactCount = 0;
		for (uint32_t pairIndex = 0; pairIndex < pairCount; ++pairIndex)
		{
			const ColliderProxy& proxyA = m_proxies[m_pairs[pairIndex].indexA];
			const ColliderProxy& proxyB = m_proxies[m_pairs[pairIndex].indexB];

			Contact contact;
			if (!Intersect(proxyA.shape, proxyB.shape, &contact))
			{
				continue;
			}

			if (m_contactCount >= m_maxContactCount)
			{
				FANG_LOG_WARNING(Collision, "接触の上限に達したので打ち切った: {} 件", m_contactCount);
				break;
			}

			contact.userIndexA = proxyA.userIndex;
			contact.userIndexB = proxyB.userIndex;

			m_contacts[m_contactCount] = contact;
			++m_contactCount;
		}
	}


	bool CollisionWorld::Raycast(
		const Vector3&     origin,
		const Vector3&     direction,
		float              maxDistance,
		const QueryFilter& filter,
		RaycastHit*        outHit
	) const
	{
		FANG_ASSERT(outHit != nullptr, "ヒットの書き込み先が null");
		FANG_ASSERT(maxDistance > 0.0f, "レイの長さが 0 以下");

		Aabb rayBounds;
		rayBounds.Expand(origin);
		rayBounds.Expand(origin + direction * maxDistance);

		uint32_t       candidateIndices[MAX_QUERY_CANDIDATE_COUNT];
		const uint32_t candidateCount = GetBroadphase().QueryAabb(rayBounds, candidateIndices);

		float    nearestDistance = maxDistance;
		Vector3  nearestNormal;
		bool     hasHit           = false;
		uint32_t nearestUserIndex = 0;

		for (uint32_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
		{
			const uint32_t index = candidateIndices[candidateIndex];
			if (!PassesFilter(m_proxies[index], filter))
			{
				continue;
			}

			// 箱で落としてから形の式に進む。
			if (!IntersectsRayWithAabb(m_bounds[index], origin, direction, nearestDistance))
			{
				continue;
			}

			float   distance = 0.0f;
			Vector3 normal;
			if (!RaycastShape(m_proxies[index].shape, origin, direction, nearestDistance, &distance, &normal))
			{
				continue;
			}

			if (hasHit && distance >= nearestDistance)
			{
				continue;
			}

			nearestDistance  = distance;
			nearestNormal    = normal;
			nearestUserIndex = m_proxies[index].userIndex;
			hasHit           = true;
		}

		if (!hasHit)
		{
			return false;
		}

		outHit->userIndex = nearestUserIndex;
		outHit->distance  = nearestDistance;
		outHit->normal    = nearestNormal;
		outHit->point     = origin + direction * nearestDistance;

		return true;
	}


	uint32_t CollisionWorld::OverlapSphere(
		const Sphere&       sphere,
		const QueryFilter&  filter,
		std::span<uint32_t> outUserIndices
	) const
	{
		const ColliderShape probe       = MakeColliderShape(sphere);
		const Aabb          probeBounds = ComputeBounds(probe);

		uint32_t       candidateIndices[MAX_QUERY_CANDIDATE_COUNT];
		const uint32_t candidateCount = GetBroadphase().QueryAabb(probeBounds, candidateIndices);

		uint32_t writtenCount = 0;
		for (uint32_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
		{
			const uint32_t index = candidateIndices[candidateIndex];
			if (!PassesFilter(m_proxies[index], filter))
			{
				continue;
			}

			Contact contact;
			if (!Intersect(probe, m_proxies[index].shape, &contact))
			{
				continue;
			}

			if (writtenCount >= outUserIndices.size())
			{
				FANG_LOG_WARNING(Collision, "球の重なりの書き込み先が足りず打ち切った: {} 件", writtenCount);
				return writtenCount;
			}

			outUserIndices[writtenCount] = m_proxies[index].userIndex;
			++writtenCount;
		}

		return writtenCount;
	}


	SweepResult CollisionWorld::SweepSphere(
		const Sphere&       sphere,
		const Vector3&      motion,
		const QueryFilter&  filter,
		std::span<SweepHit> outHits
	) const
	{
		// 球は潰れたカプセルとして同じ経路を通る。
		return SweepCapsule(
			Capsule{ .pointA = sphere.center, .pointB = sphere.center, .radius = sphere.radius },
			motion,
			filter,
			outHits
		);
	}


	SweepResult CollisionWorld::SweepCapsule(
		const Capsule&      capsule,
		const Vector3&      motion,
		const QueryFilter&  filter,
		std::span<SweepHit> outHits
	) const
	{
		const Capsule capsuleAtEnd{
			.pointA = capsule.pointA + motion,
			.pointB = capsule.pointB + motion,
			.radius = capsule.radius,
		};

		const Aabb boundsAtStart = ComputeBounds(MakeColliderShape(capsule));
		const Aabb boundsAtEnd   = ComputeBounds(MakeColliderShape(capsuleAtEnd));

		Aabb sweptBounds;
		sweptBounds.Expand(boundsAtStart.min);
		sweptBounds.Expand(boundsAtStart.max);
		sweptBounds.Expand(boundsAtEnd.min);
		sweptBounds.Expand(boundsAtEnd.max);

		uint32_t       candidateIndices[MAX_QUERY_CANDIDATE_COUNT];
		const uint32_t candidateCount = GetBroadphase().QueryAabb(sweptBounds, candidateIndices);

		SweepResult result;

		for (uint32_t candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
		{
			const uint32_t index = candidateIndices[candidateIndex];
			if (!PassesFilter(m_proxies[index], filter))
			{
				continue;
			}

			SweepHit hit;
			if (!SweepAgainstShape(capsule, motion, m_proxies[index].shape, &hit))
			{
				continue;
			}

			hit.userIndex = m_proxies[index].userIndex;
			InsertSweepHitByDistance(hit, outHits, &result);
		}

		return result;
	}


	bool CollisionWorld::HasLineOfSight(
		const Vector3&     fromPosition,
		const Vector3&     toPosition,
		const QueryFilter& filter,
		RaycastHit*        outBlockingHit
	) const
	{
		FANG_ASSERT(outBlockingHit != nullptr, "遮蔽ヒットの書き込み先が null");

		const Vector3 offset   = toPosition - fromPosition;
		const float   distance = Length(offset);
		if (distance <= DEGENERATE_MAGNITUDE)
		{
			return true;
		}

		// 対象の手前で止める。ちょうど到達点にある登録(対象自身)を遮蔽として拾わないため。
		const float clippedDistance = distance - LINE_OF_SIGHT_TARGET_MARGIN;
		if (clippedDistance <= 0.0f)
		{
			return true;
		}

		return !Raycast(fromPosition, offset * (1.0f / distance), clippedDistance, filter, outBlockingHit);
	}
} // namespace fang
