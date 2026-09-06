/**
 * @file Meat.cpp
 * @brief 肉 1 個ぶんの Scene オブジェクトの生成。
 */
#include "Meat.h"
#include "Scene/Scene.h"
#include "Wolf.h"


namespace fang::game
{
	namespace
	{
		/** @brief 狼の体長204cmを縮める倍率。51cmになる。 */
		constexpr float MEAT_SCALE = 0.25f;
	} // namespace


	Actor CreateMeatObject(Scene& scene, const WolfModel& model, const Vector3& position)
	{
		const ActorHandle handle = scene.CreateObject();
		if (!handle.IsValid())
		{
			return Actor{};
		}

		// SetLocalTransform はスケールを渡せないので、単位行列の対角3つと平行移動を直接書く。
		Matrix4x4 localMatrix;
		localMatrix.m[0][0] = MEAT_SCALE;
		localMatrix.m[1][1] = MEAT_SCALE;
		localMatrix.m[2][2] = MEAT_SCALE;
		localMatrix.m[3][0] = position.x;
		localMatrix.m[3][1] = position.y;
		localMatrix.m[3][2] = position.z;
		(void)scene.SetLocalMatrix(handle, localMatrix);

		if (model.mesh.IsValid())
		{
			const MeshRendererComponent meshComponent{
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
				.castsShadow = false, // 地面の小物を光の箱に含めない(Stage.cppと同じ理由)。
				.isVisible   = true,
			};
			(void)scene.AddMeshRendererComponent(handle, meshComponent);
		}

		return Actor(scene, handle);
	}
} // namespace fang::game
