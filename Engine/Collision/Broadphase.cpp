/**
 * @file Broadphase.cpp
 * @brief 1 軸スイープによる候補の組の絞り込み。
 */
#include "Pch.h"
#include "Collision/Broadphase.h"
#include "Collision/CollisionLog.h"
#include "Core/Memory/Allocator.h"


namespace fang
{
	namespace
	{
		/** @brief 2 つの箱が Y と Z の両方で重なっているか。X は並びで既に絞れている。 */
		bool OverlapsOnYAndZ(const Aabb& left, const Aabb& right)
		{
			return left.min.y <= right.max.y && right.min.y <= left.max.y && left.min.z <= right.max.z &&
				   right.min.z <= left.max.z;
		}
	} // namespace


	SweepAndPruneBroadphase::~SweepAndPruneBroadphase()
	{
		Shutdown();
	}


	bool SweepAndPruneBroadphase::Initialize(IAllocator& allocator, uint32_t maxColliderCount)
	{
		FANG_ASSERT(m_allocator == nullptr, "二重に初期化しようとしている");

		if (maxColliderCount == 0)
		{
			FANG_LOG_ERROR(Collision, "Broadphase の上限が 0 だ");
			return false;
		}

		m_bounds = NewArray<Aabb>(allocator, maxColliderCount);
		if (m_bounds == nullptr)
		{
			FANG_LOG_ERROR(Collision, "Broadphase の箱の置き場を確保できなかった: {} 個", maxColliderCount);
			return false;
		}

		m_order = NewArray<uint32_t>(allocator, maxColliderCount);
		if (m_order == nullptr)
		{
			DeleteArray(allocator, m_bounds, maxColliderCount);
			m_bounds = nullptr;

			FANG_LOG_ERROR(Collision, "Broadphase の並びを確保できなかった: {} 個", maxColliderCount);
			return false;
		}

		m_allocator     = &allocator;
		m_capacity      = maxColliderCount;
		m_colliderCount = 0;

		return true;
	}


	void SweepAndPruneBroadphase::Shutdown()
	{
		if (m_allocator == nullptr)
		{
			return;
		}

		DeleteArray(*m_allocator, m_order, m_capacity);
		DeleteArray(*m_allocator, m_bounds, m_capacity);

		m_order         = nullptr;
		m_bounds        = nullptr;
		m_allocator     = nullptr;
		m_capacity      = 0;
		m_colliderCount = 0;
	}


	void SweepAndPruneBroadphase::Build(std::span<const Aabb> bounds)
	{
		FANG_ASSERT(m_allocator != nullptr, "初期化していない Broadphase に箱を渡そうとしている");

		const uint32_t requestedCount = static_cast<uint32_t>(bounds.size());
		const uint32_t acceptedCount  = (requestedCount < m_capacity) ? requestedCount : m_capacity;
		if (requestedCount > acceptedCount)
		{
			FANG_LOG_WARNING(
				Collision,
				"Broadphase の上限を超えた分を捨てた: {} 個中 {} 個",
				requestedCount,
				acceptedCount
			);
		}

		// 数が変わったフレームは並びを作り直す。変わっていなければ前のフレームの並びから始める
		// ➡ ほとんど動かない置き物では、下の挿入ソートが 1 回も交換せずに終わる。
		if (acceptedCount != m_colliderCount)
		{
			for (uint32_t index = 0; index < acceptedCount; ++index)
			{
				m_order[index] = index;
			}
		}

		for (uint32_t index = 0; index < acceptedCount; ++index)
		{
			m_bounds[index] = bounds[index];
		}

		m_colliderCount = acceptedCount;

		// min.x の昇順に挿入ソート。ほぼ整列済みの列に強く、同じ並びを使い回せる。
		for (uint32_t sortedCount = 1; sortedCount < m_colliderCount; ++sortedCount)
		{
			const uint32_t movingIndex = m_order[sortedCount];
			const float    movingMinX  = m_bounds[movingIndex].min.x;

			uint32_t insertPosition = sortedCount;
			while (insertPosition > 0 && m_bounds[m_order[insertPosition - 1]].min.x > movingMinX)
			{
				m_order[insertPosition] = m_order[insertPosition - 1];
				--insertPosition;
			}

			m_order[insertPosition] = movingIndex;
		}
	}


	uint32_t SweepAndPruneBroadphase::CollectPairs(std::span<ColliderPair> outPairs) const
	{
		uint32_t pairCount = 0;

		for (uint32_t sweepIndex = 0; sweepIndex < m_colliderCount; ++sweepIndex)
		{
			const uint32_t indexA  = m_order[sweepIndex];
			const Aabb&    boundsA = m_bounds[indexA];

			// 並びは min.x の昇順 ➡ 相手の min.x が自分の max.x を越えたら、それより後ろは全部離れている。
			for (uint32_t otherIndex = sweepIndex + 1; otherIndex < m_colliderCount; ++otherIndex)
			{
				const uint32_t indexB  = m_order[otherIndex];
				const Aabb&    boundsB = m_bounds[indexB];

				if (boundsB.min.x > boundsA.max.x)
				{
					break;
				}

				if (!OverlapsOnYAndZ(boundsA, boundsB))
				{
					continue;
				}

				if (pairCount >= outPairs.size())
				{
					FANG_LOG_WARNING(Collision, "候補の組の上限に達したので打ち切った: {} 組", pairCount);
					return pairCount;
				}

				// 組の中は番号の小さいほうを先にする ➡ 呼び出し側が並びを気にしなくてよい。
				outPairs[pairCount] = (indexA < indexB) ? ColliderPair{ .indexA = indexA, .indexB = indexB }
														: ColliderPair{ .indexA = indexB, .indexB = indexA };
				++pairCount;
			}
		}

		return pairCount;
	}
} // namespace fang
