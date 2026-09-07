/**
 * @file Meat.cpp
 * @brief 肉 1 個ぶんの Scene オブジェクトの生成。
 */
#include "Meat.h"
#include "Scene/Scene.h"


namespace fang::game
{
	Actor CreateMeatObject(Scene& scene, MeshId mesh, const Aabb& localBounds)
	{
		const ActorHandle handle = scene.CreateObject();
		if (!handle.IsValid())
		{
			return Actor{};
		}

		if (mesh.IsValid())
		{
			const MeshRendererComponent meshComponent{
				.mesh        = mesh,
				.localBounds = localBounds,
				.castsShadow = false, // 地面の小物を光の箱に含めない(Stage.cppと同じ理由)。
				.isVisible   = true,
			};
			(void)scene.AddMeshRendererComponent(handle, meshComponent);
		}

		return Actor(scene, handle);
	}
} // namespace fang::game
