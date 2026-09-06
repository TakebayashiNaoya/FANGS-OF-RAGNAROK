/**
 * @file Enemy.cpp
 * @brief 雑魚 1 体の Scene オブジェクトとしての生成。狼のモデルを流用する。
 */
#include "Enemy.h"
#include "Core/Math/Matrix4x4.h"
#include "CollisionAttribute.h"
#include "EnemyController.h"
#include "Wolf.h"


namespace fang::game
{
	CharacterCreateResult<EnemyController> CreateEnemyObject(
		Scene&                  scene,
		WolfModel&              model,
		const EnemyParameter&   parameter,
		CollisionWorld*         collisionWorld,
		const HeightmapTerrain* terrain,
		const ActorHandle*      targetHandle,
		const Vector3&          initialPosition
	)
	{
		const HealthComponent health{
			.maximumHitPoints = parameter.maximumHitPoints,
			.currentHitPoints = parameter.maximumHitPoints,
		};
		const CharacterDesc desc =
			MakeCharacterDesc(model, COLLISION_ATTRIBUTE_CHARACTER | COLLISION_ATTRIBUTE_ENEMY, health);

		const EnemyController::Dependencies dependencies{
			.parameter    = &parameter,
			.targetHandle = targetHandle,
			.skinningMatricesStorage =
				model.isSkinned ? std::span<const Matrix4x4>(model.skinningMatrices) : std::span<const Matrix4x4>{},
		};

		return CreateCharacter<EnemyController>(scene, desc, dependencies, collisionWorld, terrain, initialPosition);
	}
} // namespace fang::game
