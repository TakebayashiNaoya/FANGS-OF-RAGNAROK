/**
 * @file CollisionWorld.h
 * @brief 毎フレーム受け取ったコライダーの接触を作り、クエリに答える入れ物。
 */
#pragma once

#include "Collision/Broadphase.h"
#include "Collision/CollisionShapes.h"
#include "Collision/Narrowphase.h"
#include "Core/CoreMacros.h"
#include "Core/Math/Vector3.h"
#include <cstdint>
#include <span>


namespace fang
{
	class IAllocator;

	/**
	 * @brief 1 フレームぶんのコライダー 1 個。
	 * @details オブジェクトを知らないので、身元は呼び出し側が付けた番号だけ。
	 */
	struct ColliderProxy
	{
		ColliderShape shape;

		/** @brief 呼び出し側の番号。返す接触・ヒット・重なりにそのまま入れて返す。 */
		uint32_t userIndex = 0;
	};

	/**
	 * @brief CollisionWorld の生成条件。
	 * @details 上限は起動時に決め切る。実行中のヒープ確保を 0 にするため。
	 */
	struct CollisionWorldDesc
	{
		uint32_t maxColliderCount = 1024; /**< 1 フレームに登録できる数。 */
		uint32_t maxPairCount     = 4096; /**< Broadphase が返せる候補の組の数。 */
		uint32_t maxContactCount  = 4096; /**< 1 フレームに返せる接触の数。 */
	};

	/** @brief レイキャストの結果。 */
	struct RayHit
	{
		uint32_t userIndex = 0; /**< 当たったコライダーの呼び出し側の番号。 */

		Vector3 point;  /**< ワールド空間の交点。 */
		Vector3 normal; /**< 当たった面の外向き。始点が形の中なら -direction。 */

		float distance = 0.0f; /**< 始点から交点までの距離。始点が形の中なら 0。 */
	};

	/**
	 * @brief コライダーの入れ物。毎フレーム丸ごと受け取り直して接触を作る。
	 * @details オブジェクトを知らず、渡された配列だけを見る ➡ オブジェクトモデルが GameObject でも ECS でも
	 *          この中は変わらない。押し戻しやダメージは返さない。返すのは接触情報だけ。
	 * @threading Initialize / Shutdown / Update はメインスレッドのみ。Update が戻った後の GetContacts /
	 *            Raycast / OverlapSphere は複数のジョブから同時に呼んでよい（内部状態を書かない）。
	 *            Update の最中に読まないこと。
	 */
	class CollisionWorld
	{
	public:
		FANG_NON_COPYABLE(CollisionWorld);
		FANG_NON_MOVABLE(CollisionWorld);

		CollisionWorld() = default;
		~CollisionWorld();

		/** @brief 直近の Update が受け取った数。上限で切られた後の数。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetColliderCount() const { return m_colliderCount; }

		/** @brief 直近の Update が作った接触。次の Update まで有効。 */
		[[nodiscard]] std::span<const Contact> GetContacts() const;

		/** @brief 使っている Broadphase の名前。ログとパネル用。 */
		[[nodiscard]] const char* GetBroadphaseName() const { return m_sweepAndPruneBroadphase.GetName(); }


	public:
		/**
		 * @brief 入れ物を確保する。
		 * @param allocator Shutdown まで生きていること。ロード時のヒープを想定している。
		 * @param desc      上限。0 を含んでいたら失敗する。
		 * @return 確保できなければ false。そのときは何も抱えていない。
		 */
		[[nodiscard]] bool Initialize(IAllocator& allocator, const CollisionWorldDesc& desc);

		/** @brief 入れ物を返す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief そのフレームのコライダーを全部受け取り、接触を作り直す。
		 * @param proxies 登録するコライダー。上限を超えたぶんは捨てて警告を出す。
		 * @details ヒープ確保はしない。呼び出し側の配列は写して持つので、戻った後に捨ててよい。
		 */
		void Update(std::span<const ColliderProxy> proxies);

		/**
		 * @brief 最も近い交差を 1 つ返す。
		 * @param origin      始点。
		 * @param direction   正規化した向き。
		 * @param maxDistance 見る距離。0 より大きいこと。
		 * @param outHit      当たったときだけ書く。null は不可。
		 * @return 当たらなければ false。
		 */
		[[nodiscard]] bool Raycast(
			const Vector3& origin,
			const Vector3& direction,
			float          maxDistance,
			RayHit*        outHit
		) const;

		/**
		 * @brief 球に重なったコライダーの userIndex を書き出す。
		 * @param sphere         調べる範囲。
		 * @param outUserIndices 書き込み先。
		 * @return 書いた数。outUserIndices を使い切ったらそこで打ち切る。
		 */
		[[nodiscard]] uint32_t OverlapSphere(const Sphere& sphere, std::span<uint32_t> outUserIndices) const;


	private:
		/**
		 * @brief 候補を絞る仕組みの呼び出し口。
		 * @details 実装を差し替えるときは下のメンバの型を変えるだけで、Update の中は変わらない。
		 */
		[[nodiscard]] FANG_FORCEINLINE IBroadphase& GetBroadphase() { return m_sweepAndPruneBroadphase; }

		IAllocator* m_allocator = nullptr; /**< 借用。Shutdown で返すときにも同じものを使う。 */

		ColliderProxy* m_proxies  = nullptr; /**< そのフレームの登録。呼び出し側の span を持ち越さない。 */
		Aabb*          m_bounds   = nullptr; /**< 登録ごとの包む箱。Broadphase の入力。 */
		ColliderPair*  m_pairs    = nullptr; /**< Broadphase が返した候補の組。 */
		Contact*       m_contacts = nullptr; /**< Narrowphase が通した接触。 */

		/** @brief 今の Broadphase の実体。差し替えるときはこの型を変える。 */
		SweepAndPruneBroadphase m_sweepAndPruneBroadphase;

		uint32_t m_maxColliderCount = 0;
		uint32_t m_maxPairCount     = 0;
		uint32_t m_maxContactCount  = 0;

		uint32_t m_colliderCount = 0;
		uint32_t m_contactCount  = 0;
	};
} // namespace fang
