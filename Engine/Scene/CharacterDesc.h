/**
 * @file CharacterDesc.h
 * @brief キャラクター 1 体ぶんの生成条件と、記述子どおりに組み立てる生成関数。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Collision/CollisionShapes.h"
#include "Scene/Actor.h"
#include "Scene/ComponentTypes.h"
#include "Scene/Scene.h"
#include <cstdint>
#include <utility>


namespace fang
{
	/**
	 * @brief キャラクター 1 体ぶんの生成条件。
	 * @details 読むのは生成の瞬間だけ。種類を 1 つ増やすのに書くのはこれと振る舞いの型だけになる。
	 */
	struct CharacterDesc
	{
		/** @brief 見た目。mesh が無効なら見た目もコライダーも足さない（Transform だけのオブジェクトになる）。 */
		MeshRendererComponent renderer;

		/** @brief 当たりの形。コライダーの localBounds は renderer.localBounds をそのまま使う。 */
		EnShapeType shapeType = EnShapeType::Capsule;

		/** @brief 種別のビット。意味を決めるのは Game（ADR-031）。 */
		uint32_t attributeMask = ALL_COLLISION_ATTRIBUTE_MASK;

		/** @brief 湧いたときの HP と無敵時間。 */
		HealthComponent health;
	};

	/** @brief 生成の結果。actor が無効なら Scene には何も残っていない。 */
	template <typename TBehavior> struct CharacterCreateResult
	{
		Actor      actor;
		TBehavior* behavior = nullptr; /**< 寿命は Scene が持つ。呼び出し側は解放しない。 */
	};

	/**
	 * @brief 記述子どおりのコンポーネントだけを付ける。振る舞いは載せない。
	 * @return 失敗したら無効な Actor（作りかけを残さない）。
	 * @threading 更新ジョブ 1 本から。
	 */
	[[nodiscard]] Actor CreateCharacterActor(Scene& scene, const CharacterDesc& desc);

	/**
	 * @brief 記述子どおりのコンポーネントを付けたオブジェクトを 1 つ作り、振る舞いを 1 個載せる。
	 * @details 途中で失敗したら作りかけを破棄して無効な Actor を返す ➡ 半端なオブジェクトが場に残らない。
	 * @threading 更新ジョブ 1 本から。実行中のヒープ確保は 0（席も振る舞いのブロックも確保済み）。
	 */
	template <typename TBehavior, typename... Args>
	[[nodiscard]] CharacterCreateResult<TBehavior> CreateCharacter(
		Scene&               scene,
		const CharacterDesc& desc,
		Args&&... args
	)
	{
		Actor actor = CreateCharacterActor(scene, desc);
		if (!actor.IsValid())
		{
			return CharacterCreateResult<TBehavior>{};
		}

		TBehavior* behavior = scene.AddBehavior<TBehavior>(actor.GetHandle(), std::forward<Args>(args)...);
		if (behavior == nullptr)
		{
			actor.Destroy();
			return CharacterCreateResult<TBehavior>{};
		}

		return CharacterCreateResult<TBehavior>{ .actor = actor, .behavior = behavior };
	}
} // namespace fang
