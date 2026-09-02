/**
 * @file ParallelForTests.cpp
 * @brief ParallelFor のテスト。境界値と、ワーカー数を変えても結果が変わらないこと。
 */
#include "Core/Job/JobSystem.h"
#include "Core/Job/ParallelFor.h"
#include <doctest.h>
#include <atomic>
#include <vector>


namespace
{
	/** @brief [0, elementCount) の総和をワーカーごとに足し込み、合計を返す。 */
	uint64_t SumIndicesInParallel(fang::JobSystem& jobSystem, uint32_t elementCount, uint32_t batchSize)
	{
		// ワーカーごとに置き場を分ければ、足す順番が変わっても結果は同じになる。
		std::vector<uint64_t> partialSums(jobSystem.GetExecutorCount(), 0);
		uint64_t*             partialSumsData = partialSums.data();

		fang::ParallelFor(
			jobSystem,
			0,
			elementCount,
			batchSize,
			[partialSumsData](uint32_t index, uint32_t workerIndex) { partialSumsData[workerIndex] += index; }
		);

		uint64_t totalSum = 0;
		for (const uint64_t partialSum : partialSums)
		{
			totalSum += partialSum;
		}

		return totalSum;
	}


	/** @brief 0 から count − 1 までの総和。 */
	constexpr uint64_t GetExpectedSum(uint64_t count)
	{
		return count == 0 ? 0 : count * (count - 1) / 2;
	}
} // namespace


TEST_CASE("ParallelFor は要素数 0 / 1 / 100 万のどれでも正しい")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 4 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	CHECK(SumIndicesInParallel(jobSystem, 0, 1) == GetExpectedSum(0));
	CHECK(SumIndicesInParallel(jobSystem, 1, 1) == GetExpectedSum(1));
	CHECK(SumIndicesInParallel(jobSystem, 1, 64) == GetExpectedSum(1));
	CHECK(SumIndicesInParallel(jobSystem, 2, 1) == GetExpectedSum(2));
	CHECK(SumIndicesInParallel(jobSystem, 999, 100) == GetExpectedSum(999));

	// 1 件だけ端数が出る割り方も確かめる。
	CHECK(SumIndicesInParallel(jobSystem, 1001, 100) == GetExpectedSum(1001));

	// 100 万件。ジョブ 1 件が 1,024 要素なので 977 件に割れる。
	CHECK(SumIndicesInParallel(jobSystem, 1000000, 1024) == GetExpectedSum(1000000));

	jobSystem.Shutdown();
}


TEST_CASE("ワーカー数を変えても ParallelFor の結果は変わらない")
{
	constexpr uint32_t ELEMENT_COUNT = 250000;
	constexpr uint32_t BATCH_SIZE    = 256;

	uint64_t sumWithOneWorker = 0;
	{
		fang::JobSystem jobSystem;
		if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 1 }))
		{
			CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
			return;
		}
		sumWithOneWorker = SumIndicesInParallel(jobSystem, ELEMENT_COUNT, BATCH_SIZE);
		jobSystem.Shutdown();
	}

	uint64_t sumWithEightWorkers = 0;
	{
		fang::JobSystem jobSystem;
		if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 8 }))
		{
			CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
			return;
		}
		sumWithEightWorkers = SumIndicesInParallel(jobSystem, ELEMENT_COUNT, BATCH_SIZE);
		jobSystem.Shutdown();
	}

	CHECK(sumWithOneWorker == GetExpectedSum(ELEMENT_COUNT));
	CHECK(sumWithEightWorkers == sumWithOneWorker);
}


TEST_CASE("ParallelFor はジョブの中からも呼べる")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 4 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	// 外側のジョブが内側の ParallelFor を待つ間も、そのスレッドは内側のジョブを実行する。
	std::atomic<uint64_t> innerSum{ 0 };

	struct NestedJobArguments
	{
		fang::JobSystem*       jobSystem;
		std::atomic<uint64_t>* innerSum;
	};

	NestedJobArguments jobArguments{ &jobSystem, &innerSum };

	fang::JobDesc desc{};
	desc.function = [](void* arguments, uint32_t workerIndex) {
		FANG_UNUSED(workerIndex);

		const auto&            nestedArguments = *static_cast<const NestedJobArguments*>(arguments);
		std::atomic<uint64_t>* sum             = nestedArguments.innerSum;
		fang::ParallelFor(*nestedArguments.jobSystem, 0, 10000, 128, [sum](uint32_t index, uint32_t innerWorkerIndex) {
			FANG_UNUSED(innerWorkerIndex);
			sum->fetch_add(index, std::memory_order_relaxed);
		});
	};
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);

	fang::JobCounter counter;
	jobSystem.Submit(desc, &counter);
	jobSystem.Wait(counter);

	CHECK(innerSum.load() == GetExpectedSum(10000));

	jobSystem.Shutdown();
}
