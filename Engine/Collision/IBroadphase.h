/**
 * @file IBroadphase.h
 * @brief 候補の組を絞る仕組みの差し替え口。
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

	/** @brief 候補の組を絞る仕組みの種類。CollisionWorldDesc で起動時に選ぶ。 */
	enum class EnBroadphaseType : uint8_t
	{
		SweepAndPrune,   /**< 箱を min.x で並べ、区間が重なるところだけを見る。調整値を持たない。 */
		UniformGrid,     /**< 空間を等間隔のセルに切る。セル幅は登録された箱から毎フレーム決まる。 */
		DynamicAabbTree, /**< 箱を包む木を毎フレーム建て直す。 */
	};

	/** @brief EnBroadphaseType の数。計測テストが 3 つを回すのに使う。 */
	inline constexpr uint32_t BROADPHASE_TYPE_COUNT = 3;

	/**
	 * @brief 種類を選んで実体を作る。
	 * @param allocator DestroyBroadphase まで生きていること。Initialize もこれで呼ぶこと。
	 * @return 確保できなければ nullptr。作っただけで Initialize は呼んでいない。
	 */
	[[nodiscard]] IBroadphase* CreateBroadphase(IAllocator& allocator, EnBroadphaseType type);

	/** @brief CreateBroadphase で作った実体を壊して返す。nullptr を渡してよい。 */
	void DestroyBroadphase(IAllocator& allocator, IBroadphase* broadphase);
} // namespace fang
