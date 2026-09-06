/**
 * @file EnemyDefeatRefillTests.cpp
 * @brief 湧きの補充のテスト。撃破された分だけ Scene が生存数を数え直せること、
 *        その数を SpawnScheduler へ渡すと上限まで湧きが再開することを確かめる。
 */
#include "AI/AI.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <vector>


TEST_CASE("EnemyDefeat: 32席を埋めて10席壊すと、Scene::IsValidで数え直した生存数が22になる")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 40 }));

	std::vector<fang::ActorHandle> handles;
	for (int index = 0; index < 32; ++index)
	{
		handles.push_back(scene.CreateObject());
	}

	for (int index = 0; index < 10; ++index)
	{
		scene.DestroyObject(handles[index]);
	}
	scene.Update(0.0f);

	uint32_t aliveCount = 0;
	for (const fang::ActorHandle& handle : handles)
	{
		if (scene.IsValid(handle))
		{
			++aliveCount;
		}
	}
	CHECK(aliveCount == 22);

	scene.Shutdown();
}


TEST_CASE("EnemyDefeat: 数え直した生存数をSpawnSchedulerへ渡すと、上限まで湧きが再開する")
{
	fang::SpawnScheduler scheduler;

	fang::SpawnParameter parameter{};
	parameter.intervalSeconds   = 0.1f;
	parameter.maximumAliveCount = 32;

	uint32_t aliveCount = 22; // 32 席のうち 10 席が撃破で空いた状態を模す。

	uint32_t spawnedCount = 0;
	for (int step = 0; step < 200; ++step)
	{
		const fang::SpawnRequest request = scheduler.Update(0.1f, aliveCount, fang::Vector3{}, parameter);
		if (request.shouldSpawn)
		{
			++spawnedCount;
			++aliveCount;
		}
	}

	CHECK(spawnedCount == 10);
	CHECK(aliveCount == parameter.maximumAliveCount);
}
