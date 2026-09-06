/**
 * @file CollisionWorld.h
 * @brief 毎フレーム受け取ったコライダーの接触を作り、クエリに答える入れ物。
 */
#pragma once

#include "Collision/Broadphase.h"
#include "Collision/CollisionQuery.h"
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

		/** @brief 種別のビット。既定は全ビットなので、値を入れていない登録は今までどおり全クエリに出る。 */
		uint32_t attributeMask = ALL_COLLISION_ATTRIBUTE_MASK;
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

		EnBroadphaseType broadphaseType = EnBroadphaseType::SweepAndPrune; /**< 計測で決めた既定。 */
	};

	/**
	 * @brief コライダーの入れ物。毎フレーム丸ごと受け取り直して接触を作る。
	 * @details オブジェクトを知らず、渡された配列だけを見る ➡ オブジェクトモデルが GameObject でも ECS でも
	 *          この中は変わらない。押し戻しやダメージは返さない。返すのは接触情報だけ。
	 * @threading Initialize / Shutdown はメインスレッドのみ。Update と全クエリ（GetContacts /
	 *            Raycast / OverlapSphere / Sweep* / HasLineOfSight）は更新ジョブ 1 本から呼ぶ。
	 *            クエリは const で内部状態を書かないので、同じジョブ木の中から同時に呼んでよい。
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

		/** @brief 使っている Broadphase の名前。ログとパネル用。Initialize に失敗していれば "None"。 */
		[[nodiscard]] const char* GetBroadphaseName() const
		{
			return (m_broadphase != nullptr) ? m_broadphase->GetName() : "None";
		}


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
		 * @param filter      見る相手の絞り込み。既定（QueryFilter{}）なら絞り込み前と同じ結果になる。
		 * @param outHit      当たったときだけ書く。null は不可。
		 * @return 当たらなければ false。
		 */
		[[nodiscard]] bool Raycast(
			const Vector3&     origin,
			const Vector3&     direction,
			float              maxDistance,
			const QueryFilter& filter,
			RayHit*            outHit
		) const;

		/**
		 * @brief 球に重なったコライダーの userIndex を書き出す。
		 * @param sphere         調べる範囲。
		 * @param filter         見る相手の絞り込み。
		 * @param outUserIndices 書き込み先。
		 * @return 書いた数。outUserIndices を使い切ったらそこで打ち切る。
		 */
		[[nodiscard]] uint32_t OverlapSphere(
			const Sphere&       sphere,
			const QueryFilter&  filter,
			std::span<uint32_t> outUserIndices
		) const;

		/**
		 * @brief 球を motion のぶん動かし、途中で触れた登録を近い順に書き出す。
		 * @param motion  平行移動の量。0 なら始点での重なりを答える。
		 * @param outHits 書き込み先。足りなければ近いほうから埋め、遠いぶんを捨てる。
		 */
		[[nodiscard]] SweepResult SweepSphere(
			const Sphere&       sphere,
			const Vector3&      motion,
			const QueryFilter&  filter,
			std::span<SweepHit> outHits
		) const;

		/** @brief カプセル版。中心線が潰れていれば SweepSphere と同じ結果になる。 */
		[[nodiscard]] SweepResult SweepCapsule(
			const Capsule&      capsule,
			const Vector3&      motion,
			const QueryFilter&  filter,
			std::span<SweepHit> outHits
		) const;

		/**
		 * @brief 2 点の間に遮るものが無いか。
		 * @param outBlockingHit 遮られたときだけ書く。null は不可。
		 * @return 遮るものが無ければ true。2 点が同じ位置なら常に true。
		 * @details 距離と向きの用意、対象の手前で止めること、当たらなければ見えること——この定型をここに
		 *          閉じる。視野角と索敵距離は答えない（AI の値）。発信元・対象自身の登録を数えないためには、
		 *          呼び出し側が filter.excludedUserIndices にそれぞれの userIndex を渡すこと。
		 */
		[[nodiscard]] bool HasLineOfSight(
			const Vector3&     fromPosition,
			const Vector3&     toPosition,
			const QueryFilter& filter,
			RayHit*            outBlockingHit
		) const;


	private:
		/**
		 * @brief 候補を絞る仕組みの呼び出し口。
		 * @details Initialize が成功していれば必ず非 null。
		 */
		[[nodiscard]] FANG_FORCEINLINE IBroadphase& GetBroadphase() { return *m_broadphase; }

		/** @brief const なクエリ(Raycast / OverlapSphere / Sweep* / HasLineOfSight)から使う版。 */
		[[nodiscard]] FANG_FORCEINLINE const IBroadphase& GetBroadphase() const { return *m_broadphase; }

		IAllocator* m_allocator = nullptr; /**< 借用。Shutdown で返すときにも同じものを使う。 */

		ColliderProxy* m_proxies  = nullptr; /**< そのフレームの登録。呼び出し側の span を持ち越さない。 */
		Aabb*          m_bounds   = nullptr; /**< 登録ごとの包む箱。Broadphase の入力。 */
		ColliderPair*  m_pairs    = nullptr; /**< Broadphase が返した候補の組。 */
		Contact*       m_contacts = nullptr; /**< Narrowphase が通した接触。 */

		/** @brief 今の Broadphase の実体。CreateBroadphase で desc.broadphaseType から選ぶ。 */
		IBroadphase* m_broadphase = nullptr;

		uint32_t m_maxColliderCount = 0;
		uint32_t m_maxPairCount     = 0;
		uint32_t m_maxContactCount  = 0;

		uint32_t m_colliderCount = 0;
		uint32_t m_contactCount  = 0;
	};
} // namespace fang
