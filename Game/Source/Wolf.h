/**
 * @file Wolf.h
 * @brief 狼のモデルの読み込みと、Scene オブジェクトとしての生成。
 */
#pragma once

#include "Animation/AnimationPlayback.h"
#include "Animation/SkeletalAnimation.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "RHI/RHIHandles.h"
#include "Renderer/MeshRenderer.h"
#include "Scene/CharacterDesc.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include "WolfMovementParameter.h"
#include <vector>


namespace fang
{
	class CollisionWorld;
	class HeightmapTerrain;
} // namespace fang


namespace fang::rhi
{
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang::game
{
	class WolfController;

	/**
	 * @brief 狼のメッシュ・テクスチャ・アニメーションの器。Game が読み込みから解放まで持つ。
	 * @details Scene の MeshRendererComponent は席番号とローカル境界ボックスだけを持つので、GPU リソースの
	 *          寿命はこちらが握る（終了処理でここから DestroyTexture する）。
	 */
	struct WolfModel
	{
		MeshId mesh; /**< 読み込みに失敗すると無効なままで、そのときは描画を飛ばす。 */
		Aabb   localBounds;

		/** @brief 骨を持つメッシュとして読めたか。false なら静的メッシュとして描く。 */
		bool isSkinned = false;

		rhi::TextureHandle baseColor;
		rhi::TextureHandle normalMap;

		float metallicFactor  = 0.0f;
		float roughnessFactor = 1.0f;
		float normalScale     = 1.0f;

		SkeletalAnimation animation;
		AnimationPlayback playback;

		/** @brief バインドポーズを打ち消す行列。glTF の関節の並び。読み込みのときだけ確保する。 */
		std::vector<Matrix4x4> inverseBindMatrices;

		/**
		 * @brief 毎フレーム作り直すスキニング行列。
		 * @details 単位行列で初期化してあるので、クリップを読めていなくてもバインドポーズで描ける。
		 *          置き場は読み込みのときに取り切る ➡ 毎フレームのヒープ確保は 0。複数の狼で共有する。
		 */
		std::vector<Matrix4x4> skinningMatrices;
	};

	/**
	 * @brief 狼のモデルを読んで GPU へ載せる。骨を持っていればアニメーションも読む。
	 * @details 失敗しても落とさない。モデルが出なくても三角形とエディタは動き、ゲームの本質でもないため。
	 */
	void LoadWolfModel(rhi::GraphicsDevice& device, MeshRenderer& meshRenderer, WolfModel* outWolf);

	/**
	 * @brief 読み込み済みの WolfModel から、キャラクター 1 体ぶんの記述子を作る。
	 * @details 狼も雑魚もここを通る ➡ 見た目の組み立ては Game/Source 全体でこの 1 か所だけ。
	 */
	[[nodiscard]] CharacterDesc MakeCharacterDesc(
		const WolfModel&       model,
		uint32_t               attributeMask,
		const HealthComponent& health
	);

	/**
	 * @brief 読み込み済みの WolfModel から、Scene 上のオブジェクトを 1 体作る。
	 * @param swingParameter      近接攻撃の時間割・間合い・攻撃力。
	 * @param healthComponent  湧いたときの HP と無敵時間。
	 * @param initialPosition  ワールド XZ。Y は毎フレーム地表から決める。
	 * @return 上限に達している等で作れなければ actor が無効（作りかけは残らない）。
	 * @details 作った時点では操作対象ではない。誰を操作するかは WolfManager が SetControlled で決める
	 *          （生成時と実行中で決める場所が分かれていると、引き継いだ後に食い違うため）。
	 */
	[[nodiscard]] CharacterCreateResult<WolfController> CreateWolfObject(
		Scene&                       scene,
		WolfModel&                   model,
		const WolfMovementParameter& parameter,
		const MeleeSwingParameter&   swingParameter,
		const HealthComponent&       healthComponent,
		CollisionWorld*              collisionWorld,
		const HeightmapTerrain*      terrain,
		const Vector3&               initialPosition,
		float                        initialFacingRadians
	);
} // namespace fang::game
