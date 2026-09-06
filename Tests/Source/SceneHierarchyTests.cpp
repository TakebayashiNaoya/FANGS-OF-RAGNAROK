/**
 * @file SceneHierarchyTests.cpp
 * @brief Transform 階層のテスト。親を動かすと子が動く、親を壊すと子も消える、輪を拒否する。
 */
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 2 つの行列が要素ごとにほぼ等しいか。 */
	bool AreMatricesClose(const fang::Matrix4x4& a, const fang::Matrix4x4& b)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				if (std::abs(a.m[row][column] - b.m[row][column]) > 0.0001f)
				{
					return false;
				}
			}
		}

		return true;
	}
} // namespace


TEST_CASE("SceneHierarchy: 親を動かすと子が動く")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle parent = scene.CreateObject();
	const fang::ActorHandle child  = scene.CreateObject();

	CHECK(scene.SetParent(child, parent));
	CHECK(scene.GetParent(child) == parent);

	const fang::Vector3 parentPosition{ 10.0f, 20.0f, 30.0f };
	CHECK(scene.SetLocalTransform(parent, parentPosition, 0.0f));

	scene.Update(0.0f);

	// 子はローカル単位行列のままなので、ワールドは親のワールドと一致する。
	CHECK(AreMatricesClose(scene.GetWorldMatrix(child), scene.GetWorldMatrix(parent)));
	CHECK(scene.GetWorldMatrix(child).m[3][0] == doctest::Approx(parentPosition.x));
	CHECK(scene.GetWorldMatrix(child).m[3][1] == doctest::Approx(parentPosition.y));
	CHECK(scene.GetWorldMatrix(child).m[3][2] == doctest::Approx(parentPosition.z));

	// もう一度動かしても追従する。
	const fang::Vector3 movedPosition{ -5.0f, 0.0f, 8.0f };
	CHECK(scene.SetLocalTransform(parent, movedPosition, 0.0f));
	scene.Update(0.0f);

	CHECK(scene.GetWorldMatrix(child).m[3][0] == doctest::Approx(movedPosition.x));
	CHECK(scene.GetWorldMatrix(child).m[3][2] == doctest::Approx(movedPosition.z));

	scene.Shutdown();
}


TEST_CASE("SceneHierarchy: 親を壊すと子も消える")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle parent     = scene.CreateObject();
	const fang::ActorHandle child      = scene.CreateObject();
	const fang::ActorHandle grandchild = scene.CreateObject();

	CHECK(scene.SetParent(child, parent));
	CHECK(scene.SetParent(grandchild, child));

	scene.DestroyObject(parent);
	scene.Update(0.0f);

	CHECK_FALSE(scene.IsValid(parent));
	CHECK_FALSE(scene.IsValid(child));
	CHECK_FALSE(scene.IsValid(grandchild));
	CHECK(scene.GetActiveObjectCount() == 0);

	scene.Shutdown();
}


TEST_CASE("SceneHierarchy: 輪を拒否する")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle objectA = scene.CreateObject();
	const fang::ActorHandle objectB = scene.CreateObject();

	CHECK(scene.SetParent(objectB, objectA)); // B の親は A。

	// A を B の子にすると輪ができるので拒否される。
	CHECK_FALSE(scene.SetParent(objectA, objectB));
	CHECK_FALSE(scene.GetParent(objectA).IsValid());

	// 自分自身を親にするのも拒否される。
	CHECK_FALSE(scene.SetParent(objectA, objectA));

	scene.Shutdown();
}
