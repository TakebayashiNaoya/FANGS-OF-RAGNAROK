/**
 * @file Components.h
 * @brief 汎用コンポーネント（MeshRendererComponent / ColliderComponent）と振る舞いの入口 IComponent。
 */
#pragma once

#include "Collision/CollisionShapes.h"
#include "Core/Math/Aabb.h"
#include "Core/Reflection/Reflection.h"
#include "RHI/RHIHandles.h"
#include "Renderer/MeshRenderer.h"


namespace fang
{
	class Scene;
	struct GameObjectHandle;

	/**
	 * @brief マテリアルの調整値。
	 * @details FANG_REFLECT 付きの POD。JSON はまだ読めないので既定値はコード内（03 コーディング規約 14）。
	 */
	struct MaterialParams
	{
		FANG_REFLECT_BEGIN(MaterialParams)
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

		MaterialParams materialParams;

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
	};

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
		virtual void Update(float deltaTimeSeconds, GameObjectHandle self, Scene& scene) = 0;
	};
} // namespace fang
