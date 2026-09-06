/**
 * @file DynamicAabbTreeBroadphase.h
 * @brief 箱を包む木を毎フレーム建て直して候補の組を絞る Broadphase。
 */
#pragma once

#include "Collision/Broadphase.h"
#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include <cstdint>
#include <span>


namespace fang
{
	class IAllocator;

	/**
	 * @brief 箱を包む二分木を毎フレーム建て直す Broadphase。
	 * @details 入れっぱなしにする使い方(葉を太らせて組み替えを減らす)ではなく、毎フレーム空にしてから
	 *          登録数ぶん InsertLeaf を呼び直す。挿入は表面積の増分が小さい枝を降りる分枝限定で、
	 *          高さの差が 2 になった節は回して均す。
	 * @threading Initialize / Shutdown / Build はメインスレッドのみ。CollectPairs / QueryAabb は const で、
	 *            Build が戻った後なら複数のジョブから同時に呼んでよい。
	 */
	class DynamicAabbTreeBroadphase final : public IBroadphase
	{
	public:
		FANG_NON_COPYABLE(DynamicAabbTreeBroadphase);
		FANG_NON_MOVABLE(DynamicAabbTreeBroadphase);

		DynamicAabbTreeBroadphase() = default;
		~DynamicAabbTreeBroadphase() override;

		/** @brief 人が読む名前。常に "DynamicAabbTree"。 */
		[[nodiscard]] const char* GetName() const override { return "DynamicAabbTree"; }


	public:
		[[nodiscard]] bool Initialize(IAllocator& allocator, uint32_t maxColliderCount) override;
		void               Shutdown() override;
		void               Build(std::span<const Aabb> bounds) override;

		[[nodiscard]] uint32_t CollectPairs(std::span<ColliderPair> outPairs) const override;
		[[nodiscard]] uint32_t QueryAabb(const Aabb& bounds, std::span<uint32_t> outIndices) const override;


	private:
		/** @brief 木を降りる関数の中で使う固定長スタックの深さ。回転で高さを抑えるので 4096 個でも十分足りる。 */
		static constexpr uint32_t TRAVERSAL_STACK_CAPACITY = 64;

		/** @brief 配列の中で「無い」を表す番号。 */
		static constexpr uint32_t INVALID_NODE_INDEX = 0xFFFFFFFFu;

		/**
		 * @brief 木のノード 1 個。葉も内部節もこの 1 種類で表す(child0 == INVALID_NODE_INDEX なら葉)。
		 * @details 空き節は child0 を空きリストの次の番号として使い回す。
		 */
		struct Node
		{
			Aabb bounds;

			uint32_t parent = INVALID_NODE_INDEX;
			uint32_t child0 = INVALID_NODE_INDEX; /**< 空き節のときは次の空き節の番号。 */
			uint32_t child1 = INVALID_NODE_INDEX;

			int32_t height = 0; /**< 葉は 0。空き節は -1。 */

			uint32_t boxIndex = INVALID_NODE_INDEX; /**< 葉のときだけ意味を持つ、Build に渡された箱の番号。 */
		};

		/** @brief 空きリストから 1 個取り出す。無ければ確保できていない(容量超過)。 */
		[[nodiscard]] uint32_t AllocateNode();

		/** @brief ノードを空きリストへ返す。 */
		void FreeNode(uint32_t nodeIndex);

		/** @brief 葉を作って木へ挿す。戻り値は作った葉のノード番号。 */
		uint32_t InsertLeaf(const Aabb& bounds, uint32_t boxIndex);

		/** @brief nodeIndex から根まで、包む箱と高さを作り直しながら回転で均す。 */
		void RefitAndBalanceToRoot(uint32_t nodeIndex);

		/** @brief nodeIndex の左右の高さの差が 2 以上なら回して均す。戻り値は回転後にその位置へ来た節。 */
		[[nodiscard]] uint32_t Balance(uint32_t nodeIndex);


	private:
		IAllocator* m_allocator = nullptr; /**< 借用。Shutdown で返すときにも同じものを使う。 */

		Node* m_nodes = nullptr; /**< 葉 N + 内部 N-1 ぶんの固定長プール。 */

		uint32_t* m_leafNodeIndexForBox = nullptr; /**< Build に渡した箱の番号 -> その葉のノード番号。 */

		uint32_t m_capacity  = 0; /**< maxColliderCount。 */
		uint32_t m_nodeCount = 0; /**< プールの要素数(2 × capacity)。 */

		uint32_t m_freeListHead = INVALID_NODE_INDEX;
		uint32_t m_root         = INVALID_NODE_INDEX;

		uint32_t m_colliderCount = 0;
	};
} // namespace fang
