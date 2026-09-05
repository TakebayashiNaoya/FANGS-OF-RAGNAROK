/**
 * @file Broadphase.h
 * @brief 候補の組を絞る仕組みの差し替え口と、1 軸スイープの実装。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include <cstdint>
#include <span>


namespace fang
{
	class IAllocator;

	/**
	 * @brief 箱が重なった 2 つ。
	 * @details 番号は Build に渡した並びのもの。呼び出し側の番号（ColliderProxy::userIndex）ではない。
	 */
	struct ColliderPair
	{
		uint32_t indexA = 0;
		uint32_t indexB = 0;
	};

	/**
	 * @brief 候補の組を絞る仕組み。
	 * @details 呼ばれるのは 1 フレームに Build 1 回と CollectPairs 1 回だけ ➡ 組ごとに仮想呼び出しを
	 *          通さない。実装を増やして計測で比べるときは、持ち主のメンバの型を変えるだけで済む。
	 * @threading Initialize / Shutdown / Build はメインスレッドのみ。CollectPairs は内部状態を書かない。
	 */
	class IBroadphase
	{
	public:
		virtual ~IBroadphase() = default;

		/** @brief 人が読む名前。ログ用。 */
		[[nodiscard]] virtual const char* GetName() const = 0;


	public:
		/**
		 * @brief 入れ物を確保する。
		 * @param allocator        Shutdown まで生きていること。
		 * @param maxColliderCount 1 フレームに受け取れる箱の上限。
		 * @return 確保できなければ false。
		 */
		[[nodiscard]] virtual bool Initialize(IAllocator& allocator, uint32_t maxColliderCount) = 0;

		/** @brief 入れ物を返す。二重に呼んでも安全。 */
		virtual void Shutdown() = 0;

		/**
		 * @brief そのフレームの箱を全部受け取り、内部の並びを作り直す。
		 * @param bounds ワールド空間の箱。上限を超えたぶんは捨てて警告を出す。
		 */
		virtual void Build(std::span<const Aabb> bounds) = 0;

		/**
		 * @brief 箱が重なった組を書き出す。
		 * @param outPairs 書き込み先。
		 * @return 書いた組の数。outPairs を使い切ったらそこで打ち切り、警告を出す。
		 */
		[[nodiscard]] virtual uint32_t CollectPairs(std::span<ColliderPair> outPairs) const = 0;

		/**
		 * @brief 箱に重なった登録の番号を書き出す。クエリが全登録を舐めないための入口。
		 * @return 書いた数。outIndices を使い切ったら打ち切って警告を出す。
		 */
		[[nodiscard]] virtual uint32_t QueryAabb(const Aabb& bounds, std::span<uint32_t> outIndices) const = 0;
	};

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
