/**
 * @file Minion.cpp
 * @brief 雑魚 1 体の Scene オブジェクトとしての生成。狼のモデルを流用する。
 */
#include "Minion.h"
#include "Collision/CollisionShapes.h"
#include "Core/Math/Matrix4x4.h"
#include "CollisionLayers.h"
#include "GameLog.h"
#include "MinionBehavior.h"
#include "Wolf.h"


namespace fang::game
{
	GameObjectHandle CreateMinionObject(
		Scene&                  scene,
		WolfModel&              model,
		const MinionParams&     params,
		CollisionWorld*         collisionWorld,
		const HeightmapTerrain* terrain,
		GameObjectHandle        targetHandle,
		const Vector3&          initialPosition,
		MinionBehavior**        outBehavior
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
				.materialParams =
					MaterialParams{
						.metallicFactor  = model.metallicFactor,
						.roughnessFactor = model.roughnessFactor,
						.normalScale     = model.normalScale,
					},
				.castsShadow = true,
				.isVisible   = true,
			};
			(void)scene.AddMeshRendererComponent(handle, meshRendererComponent);

			// 狼と同じく、四つ足の体を包むカプセルで当たりを取る。
			const ColliderComponent colliderComponent{
				.shapeType   = EnShapeType::Capsule,
				.localBounds = model.localBounds,
				.isEnabled   = true,
				.layerMask   = COLLISION_LAYER_CHARACTER,
			};
			(void)scene.AddColliderComponent(handle, colliderComponent);
		}

		const MinionBehavior::Dependencies dependencies{
			.params         = &params,
			.collisionWorld = collisionWorld,
			.terrain        = terrain,
			.targetHandle   = targetHandle,
			.skinningMatricesStorage =
				model.isSkinned ? std::span<const Matrix4x4>(model.skinningMatrices) : std::span<const Matrix4x4>{},
		};

		MinionBehavior* behavior = scene.AddBehavior<MinionBehavior>(handle, dependencies, initialPosition);
		if (behavior == nullptr)
		{
			FANG_LOG_ERROR(Game, "雑魚の振る舞いを作れなかった（Scene の振る舞い上限）");
		}

		if (outBehavior != nullptr)
		{
			*outBehavior = behavior;
		}

		return handle;
	}
} // namespace fang::game
