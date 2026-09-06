/**
 * @file ActorTests.cpp
 * @brief Actor（Scene 上のオブジェクトへの窓）の意味論。消えた席と既定構築への 5 通りの呼び出しを確かめる。
 */
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <array>


namespace
{
	/** @brief Scene 所有のメモリへの生ポインタを 1 本も持たない（Scene* と ActorHandle だけ）。 */
	static_assert(sizeof(fang::Actor) == sizeof(fang::Scene*) + sizeof(fang::ActorHandle));

	/** @brief 消えた席、または既定構築の窓が、5 通りの呼び出しに既定値を返すことを確かめる。 */
	void CheckActorReturnsDefaults(const fang::Actor& actor)
	{
		fang::Actor mutableActor = actor;

		CHECK_FALSE(mutableActor.IsValid());
		CHECK_FALSE(mutableActor.SetTransform(fang::Vector3{ 1.0f, 2.0f, 3.0f }, 0.5f));

		const fang::Matrix4x4 worldMatrix = mutableActor.GetWorldMatrix();
		CHECK(worldMatrix.m[0][0] == 1.0f);
		CHECK(worldMatrix.m[1][1] == 1.0f);
		CHECK(worldMatrix.m[2][2] == 1.0f);
		CHECK(worldMatrix.m[3][3] == 1.0f);

		const fang::Vector3 worldPosition = mutableActor.GetWorldPosition();
		CHECK(worldPosition.x == 0.0f);
		CHECK(worldPosition.y == 0.0f);
		CHECK(worldPosition.z == 0.0f);

		CHECK(mutableActor.GetMeshRendererComponent() == nullptr);
		CHECK(mutableActor.GetColliderComponent() == nullptr);
		CHECK(mutableActor.GetHealthComponent() == nullptr);

		mutableActor.Destroy(); // 無害であること（クラッシュしない）。
	}
} // namespace


TEST_CASE("Actor: 生成した窓は生きていて、Transform の読み書きができる")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle handle = scene.CreateObject();
	fang::Actor             actor(scene, handle);

	CHECK(actor.IsValid());
	CHECK(actor.GetHandle() == handle);
	CHECK(actor.GetIndex() == handle.index);

	CHECK(actor.SetTransform(fang::Vector3{ 10.0f, 0.0f, 20.0f }, 0.0f));
	scene.Update(0.0f);

	const fang::Vector3 worldPosition = actor.GetWorldPosition();
	CHECK(worldPosition.x == 10.0f);
	CHECK(worldPosition.z == 20.0f);

	CHECK(actor.GetMeshRendererComponent() == nullptr);
	CHECK(actor.GetColliderComponent() == nullptr);
	CHECK(actor.GetHealthComponent() == nullptr);

	scene.Shutdown();
}


TEST_CASE("Actor: 生成 → 窓を取る → Destroy → Update の後は、書き込みが false・読み出しが既定値")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle handle = scene.CreateObject();
	fang::Actor             actor(scene, handle);

	actor.Destroy();
	scene.Update(0.0f);

	CheckActorReturnsDefaults(actor);

	// 二度目の Destroy も無害。
	actor.Destroy();

	scene.Shutdown();
}


TEST_CASE("Actor: 既定構築の窓も、消えた席と同じ既定値を返す")
{
	const fang::Actor actor;
	CheckActorReturnsDefaults(actor);
}


TEST_CASE("Actor: FindFirstLiving / CountLiving は生きているものだけを数える")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle firstHandle  = scene.CreateObject();
	const fang::ActorHandle secondHandle = scene.CreateObject();

	scene.DestroyObject(firstHandle);
	scene.Update(0.0f);

	const std::array<fang::Actor, 2> actors{
		fang::Actor{ scene, firstHandle },
		fang::Actor{ scene, secondHandle },
	};

	const fang::Actor* firstLiving = fang::FindFirstLiving(actors);
	CHECK(firstLiving != nullptr);
	CHECK(firstLiving->GetHandle() == secondHandle);

	CHECK(fang::CountLiving(actors) == 1);

	scene.Shutdown();
}
