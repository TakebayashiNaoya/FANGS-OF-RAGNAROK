/**
 * @file SpawnSchedulerTests.cpp
 * @brief SpawnScheduler のテスト。間隔と上限、湧く位置の範囲、連続する体の間隔、フレーム落ちへの耐性を確かめる。
 */
#include "AI/AI.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


TEST_CASE("間隔どおりに1体ずつ湧き、上限で止まる")
{
	fang::SpawnScheduler scheduler;

	fang::SpawnParams params{};
	params.intervalSeconds   = 1.0f;
	params.maximumAliveCount = 3;

	uint32_t aliveCount = 0;

	// 0.5 秒ぶんでは間隔に届かない ➡ 湧かない。
	for (int step = 0; step < 30; ++step)
	{
		const fang::SpawnRequest request = scheduler.Update(1.0f / 60.0f, aliveCount, fang::Vector3{}, params);
		CHECK_FALSE(request.shouldSpawn);
	}

	// 残りぶんで 1 体目が湧く。1/60 の累積は浮動小数の丸めで 1.0 にわずかに届かないことがあるので、
	// ちょうど 30 回でなく余裕を持って回す。
	uint32_t spawnedCount = 0;
	for (int step = 0; step < 34; ++step)
	{
		const fang::SpawnRequest request = scheduler.Update(1.0f / 60.0f, aliveCount, fang::Vector3{}, params);
		if (request.shouldSpawn)
		{
			++spawnedCount;
			++aliveCount;
		}
	}
	CHECK(spawnedCount == 1);

	// 上限(3体)まで、1 秒おきに 1 体ずつ湧く。
	for (int interval = 0; interval < 5; ++interval)
	{
		for (int step = 0; step < 64; ++step)
		{
			const fang::SpawnRequest request = scheduler.Update(1.0f / 60.0f, aliveCount, fang::Vector3{}, params);
			if (request.shouldSpawn)
			{
				++spawnedCount;
				++aliveCount;
			}
		}
	}

	CHECK(spawnedCount == params.maximumAliveCount);
	CHECK(aliveCount == params.maximumAliveCount);
}


TEST_CASE("湧く位置は最小距離以上、最大距離以内")
{
	fang::SpawnScheduler scheduler;

	fang::SpawnParams params{};
	params.intervalSeconds            = 0.1f;
	params.maximumAliveCount          = 20;
	params.minimumDistanceCentimeters = 2200.0f;
	params.maximumDistanceCentimeters = 3200.0f;

	const fang::Vector3 target{ 100.0f, 0.0f, 100.0f };
	uint32_t            aliveCount = 0;

	for (int step = 0; step < 20; ++step)
	{
		const fang::SpawnRequest request = scheduler.Update(0.1f, aliveCount, target, params);
		if (!request.shouldSpawn)
		{
			continue;
		}
		++aliveCount;

		const fang::Vector3 delta{ request.position.x - target.x, 0.0f, request.position.z - target.z };
		const float         distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);

		CHECK(distance >= params.minimumDistanceCentimeters - 0.01f);
		CHECK(distance <= params.maximumDistanceCentimeters + 0.01f);
	}

	CHECK(aliveCount == params.maximumAliveCount);
}


TEST_CASE("連続する2体は近くに並ばない")
{
	fang::SpawnScheduler scheduler;

	fang::SpawnParams params{};
	params.intervalSeconds   = 0.1f;
	params.maximumAliveCount = 2;

	fang::Vector3 firstPosition;
	fang::Vector3 secondPosition;

	const fang::SpawnRequest first = scheduler.Update(0.1f, 0, fang::Vector3{}, params);
	CHECK(first.shouldSpawn);
	firstPosition = first.position;

	const fang::SpawnRequest second = scheduler.Update(0.1f, 1, fang::Vector3{}, params);
	CHECK(second.shouldSpawn);
	secondPosition = second.position;

	const fang::Vector3 delta{ secondPosition.x - firstPosition.x, 0.0f, secondPosition.z - firstPosition.z };
	const float         distanceBetween = std::sqrt(delta.x * delta.x + delta.z * delta.z);

	// 黄金角(約 137.5 度)ぶん方位が回るので、同じ半径帯でも十分に離れる。
	CHECK(distanceBetween > 500.0f);
}


TEST_CASE("フレームが飛んでも1フレームに1体しか湧かない")
{
	fang::SpawnScheduler scheduler;

	fang::SpawnParams params{};
	params.intervalSeconds   = 1.0f;
	params.maximumAliveCount = 100;

	// 間隔の 10 倍のフレーム落ちが起きても、1 回の Update では 1 体しか湧かない。
	const fang::SpawnRequest request = scheduler.Update(10.0f, 0, fang::Vector3{}, params);
	CHECK(request.shouldSpawn);
	CHECK(scheduler.GetSpawnedCount() == 1);

	// 貯まった経過時間は捨てずに残るので、次の呼び出し(0 秒経過)でもすぐ 2 体目が湧く。
	const fang::SpawnRequest next = scheduler.Update(0.0f, 1, fang::Vector3{}, params);
	CHECK(next.shouldSpawn);
	CHECK(scheduler.GetSpawnedCount() == 2);
}


TEST_CASE("1体も居ない状態、および上限に達した後も呼び続けて落ちない")
{
	fang::SpawnScheduler scheduler;

	fang::SpawnParams params{};
	params.intervalSeconds   = 0.1f;
	params.maximumAliveCount = 2;

	uint32_t aliveCount = 0;
	for (int step = 0; step < 10 && aliveCount < params.maximumAliveCount; ++step)
	{
		const fang::SpawnRequest request = scheduler.Update(0.1f, aliveCount, fang::Vector3{}, params);
		if (request.shouldSpawn)
		{
			++aliveCount;
		}
	}
	CHECK(aliveCount == params.maximumAliveCount);

	// 上限後は 1000 回呼んでも湧こうとしない。
	for (int step = 0; step < 1000; ++step)
	{
		const fang::SpawnRequest request = scheduler.Update(0.1f, aliveCount, fang::Vector3{}, params);
		CHECK_FALSE(request.shouldSpawn);
	}
}
