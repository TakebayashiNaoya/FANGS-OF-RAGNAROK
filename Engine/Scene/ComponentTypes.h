/**
 * @file ComponentTypes.h
 * @brief 汎用コンポーネント（MeshRendererComponent / ColliderComponent）と振る舞いの入口 IComponent。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Collision/CollisionShapes.h"
#include "Core/Math/Aabb.h"
#include "Core/Reflection/Reflection.h"
#include "RHI/RHIHandles.h"
#include "Renderer/MeshRenderer.h"


namespace fang
{
	class Scene;
	struct ActorHandle;

	/**
	 * @brief マテリアルの調整値。
	 * @details FANG_REFLECT 付きの POD。JSON はまだ読めないので既定値はコード内（03 コーディング規約 14）。
	 */
	struct MaterialParameter
	{
		FANG_REFLECT_BEGIN(MaterialParameter)
		FANG_FIELD(metallicFactor, "メタリック", Range(0.0f, 1.0f))
		FANG_FIELD(roughnessFactor, "ラフネス", Range(0.0f, 1.0f))
		FANG_FIELD(normalScale, "法線マップの強さ", Range(0.0f, 4.0f))
		FANG_REFLECT_END()

		float metallicFactor  = 0.0f;
		float roughnessFactor = 1.0f;
		float normalScale     = 1.0f;
	};

	/**
	 * @brief メッシュを描くための素性。Scene が詰めた配列で持つ。
	 * @details ワールド行列は持たない（Transform から毎フレーム引く）。BuildRenderItems がこれと
	 *          Transform・スキニング行列を合わせて RenderItem を組み立てる。
	 */
	struct MeshRendererComponent
	{
		MeshId mesh;        /**< MeshRenderer::CreateMesh が返した番号。 */
		Aabb   localBounds; /**< モデル空間の境界ボックス。生成時に MeshRenderer::GetLocalBounds から写す。 */

		rhi::TextureHandle baseColor;
		rhi::TextureHandle normalMap;

		MaterialParameter materialParameter;

		bool castsShadow = true; /**< false なら DrawDepth の対象から外す（床のような受け専用のもの）。 */
		bool isVisible   = true; /**< false なら配列の組み立てそのものから外す。 */
	};

	/**
	 * @brief 当たり判定に登録するための素性。Scene が詰めた配列で持つ。
	 * @details 実際の形（Sphere / Capsule / OBB）は BuildColliderProxies が Transform のワールド行列と
	 *          localBounds から毎フレーム作る。ここに置くのは形の種類と有効かどうかだけ。
	 */
	struct ColliderComponent
	{
		EnShapeType shapeType = EnShapeType::OBB;
		Aabb        localBounds;
		bool        isEnabled = true; /**< false なら配列の組み立てから外す。 */

		/** @brief 種別のビット。既定は全ビットなので、値を入れていない登録は今までどおり全クエリに出る。 */
		uint32_t attributeMask = ALL_COLLISION_ATTRIBUTE_MASK;
	};

	/**
	 * @brief 体力。
	 * @details 誰の体力かは持たない ➡ 当てる側は相手が雑魚かボスか壊せる置き物かを知らなくてよい（ADR-035）。
	 *          無敵の残り秒数も HP と同じ寿命・同じ持ち主なのでここに置く。
	 */
	struct HealthComponent
	{
		FANG_REFLECT_BEGIN(HealthComponent)
		FANG_FIELD(maximumHitPoints, "最大 HP", Range(0.0f, 1000000.0f))
		FANG_FIELD(currentHitPoints, "今の HP", Range(0.0f, 1000000.0f))
		FANG_FIELD(invincibleSeconds, "無敵時間", Range(0.0f, 60.0f))
		FANG_REFLECT_END()

		float maximumHitPoints = 100.0f;
		float currentHitPoints = 100.0f;

		/** @brief 1 回食らってから次に食らえるようになるまで。0 なら当たるたびに毎回入る。 */
		float invincibleSeconds = 0.0f;

		/** @brief 無敵の残り。Scene::Update が毎フレーム減らす。 */
		float invincibleSecondsRemaining = 0.0f;
	};

	/** @brief ダメージが入ったかどうか。 */
	struct DamageResult
	{
		bool wasApplied  = false; /**< 無敵時間で弾かれずに減らせた。 */
		bool wasDefeated = false; /**< 減らした結果が 0 以下になった。 */
	};

	/**
	 * @brief 体力を減らす。
	 * @details 無敵の残りがあれば何もしない。減らせたときは残りを invincibleSeconds へ入れ直す。
	 *          引く以外のことをしないのは変わらない（防御力・属性・レベル補正はこの関数の手前）。
	 */
	[[nodiscard]] DamageResult ApplyDamage(HealthComponent* health, float damage);

	/**
	 * @brief 無敵の残りを 1 フレームぶん減らす。
	 * @details 引き算の丸め残りで境目が 1 フレームずれないよう、ごく小さい残りは 0 へ落とす。
	 */
	void TickInvincibility(HealthComponent* health, float deltaTimeSeconds);

	/**
	 * @brief 振る舞い（Update を持つコンポーネント）の入口。
	 * @details Scene::AddBehavior<T> が固定長ブロックのプールから配る。狼の移動・アニメなど、
	 *          ゲーム固有の振る舞いはこれを継承して Game/Source/ に置く（01 アーキテクチャ 2）。
	 */
	class IComponent
	{
	public:
		virtual ~IComponent() = default;

		/**
		 * @brief 1 フレームぶん進める。
		 * @param self  この振る舞いを持つオブジェクト。Transform や他のコンポーネントを触るときに使う。
		 * @param scene 呼び出し元の Scene。
		 * @details Scene::Update が更新ジョブの中から呼ぶ。呼ばれる本数は Update の入口で固定されるので、
		 *          この中で AddBehavior したものは次の周から、DestroyObject したものはその周のうちに
		 *          呼ばれなくなる。
		 */
		virtual void Update(float deltaTimeSeconds, ActorHandle self, Scene& scene) = 0;
	};
} // namespace fang
