/**
 * @file DynamicAabbTreeBroadphase.cpp
 * @brief 動的 AABB ツリーへの挿入・回転と、木の降下による候補の組の絞り込み。
 */
#include "Pch.h"
#include "Collision/DynamicAabbTreeBroadphase.h"
#include "Collision/CollisionLog.h"
#include "Collision/CollisionMath.h"
#include "Core/Memory/Allocator.h"
#include <algorithm>


namespace fang
{
	namespace
	{
		/** @brief 2 つの箱を両方含む箱。 */
		Aabb Union(const Aabb& left, const Aabb& right)
		{
			Aabb result;
			result.min = Vector3{ std::min(left.min.x, right.min.x),
								  std::min(left.min.y, right.min.y),
								  std::min(left.min.z, right.min.z) };
			result.max = Vector3{ std::max(left.max.x, right.max.x),
								  std::max(left.max.y, right.max.y),
								  std::max(left.max.z, right.max.z) };

			return result;
		}


		/** @brief 箱の表面積。挿入先を選ぶコストに使う(絶対値に意味は無く、比較にだけ使う)。 */
		float SurfaceArea(const Aabb& bounds)
		{
			const Vector3 size = bounds.max - bounds.min;

			return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
		}
	} // namespace


	DynamicAabbTreeBroadphase::~DynamicAabbTreeBroadphase()
	{
		Shutdown();
	}


	uint32_t DynamicAabbTreeBroadphase::AllocateNode()
	{
		FANG_ASSERT(m_freeListHead != INVALID_NODE_INDEX, "ノードプールを使い切った");

		const uint32_t nodeIndex = m_freeListHead;
		m_freeListHead           = m_nodes[nodeIndex].child0;

		m_nodes[nodeIndex].parent   = INVALID_NODE_INDEX;
		m_nodes[nodeIndex].child0   = INVALID_NODE_INDEX;
		m_nodes[nodeIndex].child1   = INVALID_NODE_INDEX;
		m_nodes[nodeIndex].height   = 0;
		m_nodes[nodeIndex].boxIndex = INVALID_NODE_INDEX;

		return nodeIndex;
	}


	void DynamicAabbTreeBroadphase::FreeNode(uint32_t nodeIndex)
	{
		m_nodes[nodeIndex].child0 = m_freeListHead;
		m_nodes[nodeIndex].height = -1;
		m_freeListHead            = nodeIndex;
	}


	bool DynamicAabbTreeBroadphase::Initialize(IAllocator& allocator, uint32_t maxColliderCount)
	{
		FANG_ASSERT(m_allocator == nullptr, "二重に初期化しようとしている");

		if (maxColliderCount == 0)
		{
			FANG_LOG_ERROR(Collision, "Broadphase の上限が 0 だ");
			return false;
		}

		const uint32_t nodeCount = maxColliderCount * 2;

		m_nodes               = NewArray<Node>(allocator, nodeCount);
		m_leafNodeIndexForBox = NewArray<uint32_t>(allocator, maxColliderCount);

		const bool hasAllBuffers = m_nodes != nullptr && m_leafNodeIndexForBox != nullptr;
		if (!hasAllBuffers)
		{
			FANG_LOG_ERROR(Collision, "DynamicAabbTreeBroadphase の置き場を確保できなかった: {} 個", maxColliderCount);

			DeleteArray(allocator, m_leafNodeIndexForBox, maxColliderCount);
			DeleteArray(allocator, m_nodes, nodeCount);
			m_leafNodeIndexForBox = nullptr;
			m_nodes               = nullptr;
			return false;
		}

		m_allocator = &allocator;
		m_capacity  = maxColliderCount;
		m_nodeCount = nodeCount;

		for (uint32_t index = 0; index < nodeCount; ++index)
		{
			m_nodes[index].child0 = (index + 1 < nodeCount) ? (index + 1) : INVALID_NODE_INDEX;
			m_nodes[index].height = -1;
		}
		m_freeListHead  = 0;
		m_root          = INVALID_NODE_INDEX;
		m_colliderCount = 0;

		return true;
	}


	void DynamicAabbTreeBroadphase::Shutdown()
	{
		if (m_allocator == nullptr)
		{
			return;
		}

		DeleteArray(*m_allocator, m_leafNodeIndexForBox, m_capacity);
		DeleteArray(*m_allocator, m_nodes, m_nodeCount);

		m_leafNodeIndexForBox = nullptr;
		m_nodes               = nullptr;

		m_allocator     = nullptr;
		m_capacity      = 0;
		m_nodeCount     = 0;
		m_freeListHead  = INVALID_NODE_INDEX;
		m_root          = INVALID_NODE_INDEX;
		m_colliderCount = 0;
	}


	uint32_t DynamicAabbTreeBroadphase::InsertLeaf(const Aabb& bounds, uint32_t boxIndex)
	{
		const uint32_t leafIndex    = AllocateNode();
		m_nodes[leafIndex].bounds   = bounds;
		m_nodes[leafIndex].boxIndex = boxIndex;

		if (m_root == INVALID_NODE_INDEX)
		{
			m_root = leafIndex;
			return leafIndex;
		}

		// 分枝限定の降下: 各段で、葉を加えたときの表面積の増分が小さいほうへ進む。葉に着くまで続ける。
		uint32_t current = m_root;
		while (m_nodes[current].child0 != INVALID_NODE_INDEX)
		{
			const uint32_t child0 = m_nodes[current].child0;
			const uint32_t child1 = m_nodes[current].child1;

			const float cost0 = SurfaceArea(Union(m_nodes[child0].bounds, bounds));
			const float cost1 = SurfaceArea(Union(m_nodes[child1].bounds, bounds));

			current = (cost0 <= cost1) ? child0 : child1;
		}

		const uint32_t sibling   = current;
		const uint32_t oldParent = m_nodes[sibling].parent;
		const uint32_t newParent = AllocateNode();

		m_nodes[newParent].parent = oldParent;
		m_nodes[newParent].bounds = Union(m_nodes[sibling].bounds, bounds);
		m_nodes[newParent].height = m_nodes[sibling].height + 1;
		m_nodes[newParent].child0 = sibling;
		m_nodes[newParent].child1 = leafIndex;

		if (oldParent == INVALID_NODE_INDEX)
		{
			m_root = newParent;
		}
		else if (m_nodes[oldParent].child0 == sibling)
		{
			m_nodes[oldParent].child0 = newParent;
		}
		else
		{
			m_nodes[oldParent].child1 = newParent;
		}

		m_nodes[sibling].parent   = newParent;
		m_nodes[leafIndex].parent = newParent;

		// newParent 自身は上で確定済み。祖先だけを辿って包む箱と高さを作り直し、崩れた均衡を回す。
		RefitAndBalanceToRoot(oldParent);

		return leafIndex;
	}


	void DynamicAabbTreeBroadphase::RefitAndBalanceToRoot(uint32_t nodeIndex)
	{
		uint32_t current = nodeIndex;
		while (current != INVALID_NODE_INDEX)
		{
			current = Balance(current);

			const uint32_t child0 = m_nodes[current].child0;
			const uint32_t child1 = m_nodes[current].child1;

			m_nodes[current].bounds = Union(m_nodes[child0].bounds, m_nodes[child1].bounds);
			m_nodes[current].height = 1 + std::max(m_nodes[child0].height, m_nodes[child1].height);

			current = m_nodes[current].parent;
		}
	}


	uint32_t DynamicAabbTreeBroadphase::Balance(uint32_t nodeIndex)
	{
		if (m_nodes[nodeIndex].child0 == INVALID_NODE_INDEX)
		{
			// 葉は回さない。
			return nodeIndex;
		}

		const uint32_t child0 = m_nodes[nodeIndex].child0;
		const uint32_t child1 = m_nodes[nodeIndex].child1;

		const int32_t balanceFactor = m_nodes[child1].height - m_nodes[child0].height;
		if (balanceFactor >= -1 && balanceFactor <= 1)
		{
			return nodeIndex;
		}

		// 高いほうの子(heavyChild)を nodeIndex の位置へ持ち上げる。heavyChild の孫のうち高いほうは
		// heavyChild に残し、低いほうを nodeIndex(今は heavyChild の子)へ渡す。
		const uint32_t heavyChild = (balanceFactor > 1) ? child1 : child0;

		const uint32_t grandchild0 = m_nodes[heavyChild].child0;
		const uint32_t grandchild1 = m_nodes[heavyChild].child1;

		const uint32_t promotedGrandchild =
			(m_nodes[grandchild0].height > m_nodes[grandchild1].height) ? grandchild0 : grandchild1;
		const uint32_t demotedGrandchild = (promotedGrandchild == grandchild0) ? grandchild1 : grandchild0;

		const uint32_t originalParent = m_nodes[nodeIndex].parent;

		m_nodes[heavyChild].parent = originalParent;
		if (originalParent == INVALID_NODE_INDEX)
		{
			m_root = heavyChild;
		}
		else if (m_nodes[originalParent].child0 == nodeIndex)
		{
			m_nodes[originalParent].child0 = heavyChild;
		}
		else
		{
			m_nodes[originalParent].child1 = heavyChild;
		}

		if (balanceFactor > 1)
		{
			m_nodes[heavyChild].child0 = nodeIndex;
			m_nodes[heavyChild].child1 = promotedGrandchild;
			m_nodes[nodeIndex].child1  = demotedGrandchild;
		}
		else
		{
			m_nodes[heavyChild].child1 = nodeIndex;
			m_nodes[heavyChild].child0 = promotedGrandchild;
			m_nodes[nodeIndex].child0  = demotedGrandchild;
		}

		m_nodes[demotedGrandchild].parent = nodeIndex;
		m_nodes[nodeIndex].parent         = heavyChild;

		m_nodes[nodeIndex].bounds =
			Union(m_nodes[m_nodes[nodeIndex].child0].bounds, m_nodes[m_nodes[nodeIndex].child1].bounds);
		m_nodes[nodeIndex].height =
			1 + std::max(m_nodes[m_nodes[nodeIndex].child0].height, m_nodes[m_nodes[nodeIndex].child1].height);

		m_nodes[heavyChild].bounds =
			Union(m_nodes[m_nodes[heavyChild].child0].bounds, m_nodes[m_nodes[heavyChild].child1].bounds);
		m_nodes[heavyChild].height =
			1 + std::max(m_nodes[m_nodes[heavyChild].child0].height, m_nodes[m_nodes[heavyChild].child1].height);

		return heavyChild;
	}


	void DynamicAabbTreeBroadphase::Build(std::span<const Aabb> bounds)
	{
		FANG_ASSERT(m_allocator != nullptr, "初期化していない Broadphase に箱を渡そうとしている");

		// 木を空にする。全ノードを空きリストへ戻す。
		for (uint32_t index = 0; index < m_nodeCount; ++index)
		{
			m_nodes[index].child0 = (index + 1 < m_nodeCount) ? (index + 1) : INVALID_NODE_INDEX;
			m_nodes[index].height = -1;
		}
		m_freeListHead = 0;
		m_root         = INVALID_NODE_INDEX;

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

		m_colliderCount = acceptedCount;

		for (uint32_t index = 0; index < m_colliderCount; ++index)
		{
			m_leafNodeIndexForBox[index] = InsertLeaf(bounds[index], index);
		}
	}


	uint32_t DynamicAabbTreeBroadphase::CollectPairs(std::span<ColliderPair> outPairs) const
	{
		if (m_root == INVALID_NODE_INDEX)
		{
			return 0;
		}

		uint32_t pairCount = 0;

		for (uint32_t queryIndex = 0; queryIndex < m_colliderCount; ++queryIndex)
		{
			const Aabb& queryBounds = m_nodes[m_leafNodeIndexForBox[queryIndex]].bounds;

			uint32_t stack[TRAVERSAL_STACK_CAPACITY];
			uint32_t stackSize = 0;
			stack[stackSize++] = m_root;

			while (stackSize > 0)
			{
				const uint32_t nodeIndex = stack[--stackSize];
				const Node&    node      = m_nodes[nodeIndex];

				if (!OverlapsOnAllAxes(node.bounds, queryBounds))
				{
					continue;
				}

				if (node.child0 == INVALID_NODE_INDEX)
				{
					if (node.boxIndex > queryIndex)
					{
						if (pairCount >= outPairs.size())
						{
							FANG_LOG_WARNING(Collision, "候補の組の上限に達したので打ち切った: {} 組", pairCount);
							return pairCount;
						}

						outPairs[pairCount] = ColliderPair{ .indexA = queryIndex, .indexB = node.boxIndex };
						++pairCount;
					}

					continue;
				}

				FANG_ASSERT(stackSize + 2 <= TRAVERSAL_STACK_CAPACITY, "木の降下スタックが足りない");
				stack[stackSize++] = node.child0;
				stack[stackSize++] = node.child1;
			}
		}

		return pairCount;
	}


	uint32_t DynamicAabbTreeBroadphase::QueryAabb(const Aabb& bounds, std::span<uint32_t> outIndices) const
	{
		uint32_t writtenCount = 0;

		if (m_root == INVALID_NODE_INDEX)
		{
			return 0;
		}

		uint32_t stack[TRAVERSAL_STACK_CAPACITY];
		uint32_t stackSize = 0;
		stack[stackSize++] = m_root;

		while (stackSize > 0)
		{
			const uint32_t nodeIndex = stack[--stackSize];
			const Node&    node      = m_nodes[nodeIndex];

			if (!OverlapsOnAllAxes(node.bounds, bounds))
			{
				continue;
			}

			if (node.child0 == INVALID_NODE_INDEX)
			{
				if (writtenCount >= outIndices.size())
				{
					FANG_LOG_WARNING(Collision, "領域クエリの書き込み先が足りず打ち切った: {} 件", writtenCount);
					return writtenCount;
				}

				outIndices[writtenCount] = node.boxIndex;
				++writtenCount;

				continue;
			}

			FANG_ASSERT(stackSize + 2 <= TRAVERSAL_STACK_CAPACITY, "木の降下スタックが足りない");
			stack[stackSize++] = node.child0;
			stack[stackSize++] = node.child1;
		}

		return writtenCount;
	}
} // namespace fang
