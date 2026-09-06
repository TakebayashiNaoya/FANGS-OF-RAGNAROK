/**
 * @file CharacterDesc.cpp
 * @brief キャラクター 1 体ぶんの生成条件と、記述子どおりに組み立てる生成関数。
 */
#include "Pch.h"
#include "Scene/CharacterDesc.h"


namespace fang
{
	Actor CreateCharacterActor(Scene& scene, const CharacterDesc& desc)
	{
		const ActorHandle handle = scene.CreateObject();
		if (!handle.IsValid())
		{
			return Actor{};
		}

		Actor actor{ scene, handle };

		bool isOk = true;
		if (desc.renderer.mesh.IsValid())
		{
			isOk = scene.AddMeshRendererComponent(handle, desc.renderer);
			if (isOk)
			{
				const ColliderComponent colliderComponent{
					.shapeType     = desc.shapeType,
					.localBounds   = desc.renderer.localBounds,
					.isEnabled     = true,
					.attributeMask = desc.attributeMask,
				};
				isOk = scene.AddColliderComponent(handle, colliderComponent);
			}
		}

		if (isOk)
		{
			isOk = scene.AddHealthComponent(handle, desc.health);
		}

		if (!isOk)
		{
			actor.Destroy();
			return Actor{};
		}

		return actor;
	}
} // namespace fang
