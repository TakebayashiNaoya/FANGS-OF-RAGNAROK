/**
 * @file Enemy.cpp
 * @brief 雑魚 1 体の Scene オブジェクトとしての生成。狼のモデルを流用する。
 */
#include "Enemy.h"
#include "Collision/CollisionShapes.h"
#include "Core/Math/Matrix4x4.h"
#include "CollisionAttribute.h"
#include "EnemyController.h"
#include "GameLog.h"
#include "Wolf.h"


namespace fang::game
{
	GameObjectHandle CreateEnemyObject(
		Scene&                  scene,
		WolfModel&              model,
		const EnemyParameter&   parameter,
		CollisionWorld*         collisionWorld,
		const HeightmapTerrain* terrain,
		const GameObjectHandle* targetHandle,
		const Vector3&          initialPosition,
		EnemyController**       outController
	)
	{
		const GameObjectHandle handle = scene.CreateObject();
		if (!handle.IsValid())
		{
			FANG_LOG_WARNING(Game, "雑魚のオブジェクトを作れなかった（Scene の上限）");
			return handle;
		}

		if (model.mesh.IsValid())
		{
			const MeshRendererComponent meshRendererComponent{
				.mesh        = model.mesh,
				.localBounds = model.localBounds,
				.baseColor   = model.baseColor,
				.normalMap   = model.normalMap,
				.materialParameter =
					MaterialParameter{
						.metallicFactor  = model.metallicFactor,
						.roughnessFactor = model.roughnessFactor,
						.normalScale     = model.normalScale,
					},
				.castsShadow = true,
				.isVisible   = true,
			};
			(void)scene.AddMeshRendererComponent(handle, meshRendererComponent);

			// 狼と同じく、四つ足の体を包むカプセルで当たりを取る。ENEMY を足して攻撃の掃引に出す。
			const ColliderComponent colliderComponent{
				.shapeType     = EnShapeType::Capsule,
				.localBounds   = model.localBounds,
				.isEnabled     = true,
				.attributeMask = COLLISION_ATTRIBUTE_CHARACTER | COLLISION_ATTRIBUTE_ENEMY,
			};
			(void)scene.AddColliderComponent(handle, colliderComponent);
		}

		(void)scene.AddHealthComponent(
			handle,
			HealthComponent{ .maximumHitPoints = parameter.maximumHitPoints,
							 .currentHitPoints = parameter.maximumHitPoints }
		);

		const EnemyController::Dependencies dependencies{
			.parameter      = &parameter,
			.collisionWorld = collisionWorld,
			.terrain        = terrain,
			.targetHandle   = targetHandle,
			.skinningMatricesStorage =
				model.isSkinned ? std::span<const Matrix4x4>(model.skinningMatrices) : std::span<const Matrix4x4>{},
		};

		EnemyController* controller = scene.AddBehavior<EnemyController>(handle, dependencies, initialPosition);
		if (controller == nullptr)
		{
			FANG_LOG_ERROR(Game, "雑魚の振る舞いを作れなかった（Scene の振る舞い上限）");
		}

		if (outController != nullptr)
		{
			*outController = controller;
		}

		return handle;
	}
} // namespace fang::game
