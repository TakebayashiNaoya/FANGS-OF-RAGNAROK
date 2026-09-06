/**
 * @file Enemy.h
 * @brief 雑魚 1 体の Scene オブジェクトとしての生成。狼のモデルを流用する。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Scene/CharacterDesc.h"
#include "Scene/Scene.h"


namespace fang
{
	class CollisionWorld;
	class HeightmapTerrain;
} // namespace fang


namespace fang::game
{
	class EnemyController;
	struct EnemyParameter;
	struct WolfModel;

	/**
	 * @brief 読み込み済みの WolfModel を流用して、雑魚 1 体の Scene オブジェクトを作る。
	 * @param model        狼と共有するメッシュ・テクスチャ・スキニング行列の置き場。
	 * @param targetHandle 追いかける相手（今の操作対象）。Game が持ち替えるので、寿命は呼び出し側が持つ。
	 * @return 上限に達している等で作れなければ actor が無効（作りかけは残らない）。
	 */
	[[nodiscard]] CharacterCreateResult<EnemyController> CreateEnemyObject(
		Scene&                  scene,
		WolfModel&              model,
		const EnemyParameter&   parameter,
		CollisionWorld*         collisionWorld,
		const HeightmapTerrain* terrain,
		const ActorHandle*      targetHandle,
		const Vector3&          initialPosition
	);
} // namespace fang::game
