/**
 * @file SceneObjectTests.cpp
 * @brief Scene の生成・破棄。壊したハンドルの再利用、上限、0 体・全滅での安全性を確かめる。
 */
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>


TEST_CASE("Scene: 生成すると即座に有効になり、破棄は Update まで反映されない")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::GameObjectHandle handle = scene.CreateObject();
	CHECK(handle.IsValid());
	CHECK(scene.IsValid(handle));
	CHECK(scene.GetActiveObjectCount() == 1);

	scene.DestroyObject(handle);
	CHECK(scene.IsValid(handle)); // まだ Update していないので生きたまま。

	scene.Update(0.0f);
	CHECK_FALSE(scene.IsValid(handle));
	CHECK(scene.GetActiveObjectCount() == 0);

	scene.Shutdown();
}


TEST_CASE("Scene: 壊したハンドルで触っても、後から作った別のオブジェクトに当たらない")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::GameObjectHandle oldHandle = scene.CreateObject();
	scene.DestroyObject(oldHandle);
	scene.Update(0.0f);

	// 同じ席が再利用されても、世代が進んでいるので古いハンドルは別物として扱われる。
	const fang::GameObjectHandle newHandle = scene.CreateObject();
	CHECK(newHandle.index == oldHandle.index);
	CHECK(newHandle.generation != oldHandle.generation);

	CHECK_FALSE(scene.IsValid(oldHandle));
	CHECK(scene.IsValid(newHandle));

	scene.Shutdown();
}


TEST_CASE("Scene: 上限に達したら無効なハンドルを返す")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 2 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	CHECK(scene.CreateObject().IsValid());
	CHECK(scene.CreateObject().IsValid());

	const fang::GameObjectHandle overflowHandle = scene.CreateObject();
	CHECK_FALSE(overflowHandle.IsValid());
	CHECK_FALSE(scene.IsValid(overflowHandle));
	CHECK(scene.GetActiveObjectCount() == 2);

	scene.Shutdown();
}


TEST_CASE("Scene: オブジェクトが 0 体でも Update が安全に回る")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	scene.Update(1.0f / 60.0f);
	CHECK(scene.GetActiveObjectCount() == 0);

	scene.Shutdown();
}


TEST_CASE("Scene: 全部壊しても落ちない")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 8 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	fang::GameObjectHandle handles[8];
	for (fang::GameObjectHandle& handle : handles)
	{
		handle = scene.CreateObject();
		CHECK(handle.IsValid());
	}

	for (const fang::GameObjectHandle& handle : handles)
	{
		scene.DestroyObject(handle);
	}

	scene.Update(1.0f / 60.0f);
	CHECK(scene.GetActiveObjectCount() == 0);

	// 空いた席を全部作り直せる ➡ 空き番号の管理が破棄で壊れていない。
	for (fang::GameObjectHandle& handle : handles)
	{
		handle = scene.CreateObject();
		CHECK(handle.IsValid());
	}
	CHECK(scene.GetActiveObjectCount() == 8);

	scene.Shutdown();
}
