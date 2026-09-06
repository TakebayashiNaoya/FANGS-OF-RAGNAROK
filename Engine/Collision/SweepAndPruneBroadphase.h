/**
 * @file SweepAndPruneBroadphase.h
 * @brief 1 軸スイープによる候補の組の絞り込み。
 */
#pragma once

#include "Collision/IBroadphase.h"
#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include <cstdint>
#include <span>


namespace fang
{
	class IAllocator;

	/**
	 * @brief 箱を min.x で並べ、区間が重なるところだけを見る Broadphase。
	 * @details 調整値を持たないので、大きさがばらばらの置き物と雑魚を同じ入れ物に入れても壊れない。
	 *          並びは前のフレームのものを初期値にした挿入ソートで保つ ➡ ほとんど動かないフレームでは
	 *          比較がほぼ要素数と同じ回数で終わる。
	 * @threading Initialize / Shutdown / Build はメインスレッドのみ。CollectPairs は const で、
	 *            Build が戻った後なら複数のジョブから同時に呼んでよい。
	 */
	class SweepAndPruneBroadphase final : public IBroadphase
	{
	public:
		FANG_NON_COPYABLE(SweepAndPruneBroadphase);
		FANG_NON_MOVABLE(SweepAndPruneBroadphase);

		SweepAndPruneBroadphase() = default;
		~SweepAndPruneBroadphase() override;

		/** @brief 人が読む名前。常に "SweepAndPrune"。 */
		[[nodiscard]] const char* GetName() const override { return "SweepAndPrune"; }

		/** @brief 直近の Build が受け取った箱の数。上限で切られた後の数。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetColliderCount() const { return m_colliderCount; }


	public:
		[[nodiscard]] bool Initialize(IAllocator& allocator, uint32_t maxColliderCount) override;
		void               Shutdown() override;
		void               Build(std::span<const Aabb> bounds) override;

		[[nodiscard]] uint32_t CollectPairs(std::span<ColliderPair> outPairs) const override;
		[[nodiscard]] uint32_t QueryAabb(const Aabb& bounds, std::span<uint32_t> outIndices) const override;


	private:
		IAllocator* m_allocator = nullptr; /**< 借用。Shutdown で返すときにも同じものを使う。 */

		/** @brief そのフレームの箱。呼び出し側の span を持ち越さないよう写して持つ。 */
		Aabb* m_bounds = nullptr;

		/** @brief 箱の番号を min.x の昇順に並べたもの。前のフレームの並びを次の挿入ソートの初期値にする。 */
		uint32_t* m_order = nullptr;

		uint32_t m_capacity      = 0;
		uint32_t m_colliderCount = 0;
	};
} // namespace fang
