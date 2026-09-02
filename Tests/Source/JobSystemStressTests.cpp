/**
 * @file JobSystemStressTests.cpp
 * @brief 並列の不具合は 1 回では出ないので、乱数で形を変えながら繰り返す。
 */
#include "Core/Job/JobCounter.h"
#include "Core/Job/JobSystem.h"
#include "Core/Job/ParallelFor.h"
#include <doctest.h>
#include <atomic>
#include <vector>


namespace
{
	/** @brief 種を持ち歩く xorshift。std の乱数と違って再現できる。 */
	class TestRandom
	{
	public:
		explicit TestRandom(uint32_t seed)
			: m_state(seed | 1u)
		{
		}

		[[nodiscard]] uint32_t Next()
		{
			m_state ^= m_state << 13;
			m_state ^= m_state >> 17;
			m_state ^= m_state << 5;
			return m_state;
		}

		[[nodiscard]] uint32_t NextInRange(uint32_t minimum, uint32_t maximum)
		{
			return minimum + Next() % (maximum - minimum + 1);
		}


	private:
		uint32_t m_state = 1;
	};


	struct StressJobArguments
	{
		std::atomic<uint64_t>* sum;
		uint32_t               value;
		uint32_t               busyLoopCount;
	};


	void RunStressJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto& jobArguments = *static_cast<const StressJobArguments*>(arguments);

		uint32_t accumulator = 0;
		for (uint32_t i = 0; i < jobArguments.busyLoopCount; ++i)
		{
			accumulator += i;
		}

		if (accumulator != 0xFFFFFFFFu)
		{
			jobArguments.sum->fetch_add(jobArguments.value, std::memory_order_relaxed);
		}
	}


	struct StageObserverArguments
	{
		std::atomic<uint64_t>* firstStageSum;
		std::atomic<uint64_t>* observedSum;
	};


	void RunStageObserverJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto& jobArguments = *static_cast<const StageObserverArguments*>(arguments);
		jobArguments.observedSum->store(
			jobArguments.firstStageSum->load(std::memory_order_acquire),
			std::memory_order_release
		);
	}
} // namespace


TEST_CASE("形を変えながら繰り返しても、総和と依存の順序が崩れない")
{
	constexpr uint32_t ITERATION_COUNT = 400;

	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 8 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	TestRandom random{ 0x5EED1234u };

	uint32_t wrongSumCount      = 0;
	uint32_t wrongOrderCount    = 0;
	uint32_t wrongParallelCount = 0;

	for (uint32_t iteration = 0; iteration < ITERATION_COUNT; ++iteration)
	{
		const uint32_t firstStageJobCount = random.NextInRange(1, 600);
		const uint32_t busyLoopCount      = random.NextInRange(0, 512);

		std::atomic<uint64_t> firstStageSum{ 0 };
		std::atomic<uint64_t> observedSum{ 0xFFFFFFFFFFFFFFFFull };

		fang::JobCounter firstStageCounter;
		fang::JobCounter secondStageCounter;

		uint64_t expectedSum = 0;

		fang::JobDesc firstStageDesc{};
		firstStageDesc.function     = &RunStressJob;
		firstStageDesc.argumentSize = sizeof(StressJobArguments);

		for (uint32_t i = 0; i < firstStageJobCount; ++i)
		{
			const StressJobArguments jobArguments{ &firstStageSum, i, busyLoopCount };
			expectedSum += i;

			firstStageDesc.arguments = &jobArguments;
			jobSystem.Submit(firstStageDesc, &firstStageCounter);
		}

		// 前段の途中で後段を積む。0 到達と同時に積まれる競合はここで踏む。
		const StageObserverArguments observerArguments{ &firstStageSum, &observedSum };

		fang::JobDesc secondStageDesc{};
		secondStageDesc.function     = &RunStageObserverJob;
		secondStageDesc.arguments    = &observerArguments;
		secondStageDesc.argumentSize = sizeof(observerArguments);
		secondStageDesc.waitCounter  = &firstStageCounter;

		jobSystem.Submit(secondStageDesc, &secondStageCounter);
		jobSystem.Wait(secondStageCounter);

		if (firstStageSum.load() != expectedSum)
		{
			++wrongSumCount;
		}

		if (observedSum.load() != expectedSum)
		{
			++wrongOrderCount;
		}

		// 同じ回の中で ParallelFor も混ぜて、キューの奪い合いを増やす。
		// batchSize の下限を決めているのは、1 回のジョブ数をプールの上限より十分下に抑えるため。
		const uint32_t elementCount = random.NextInRange(0, 20000);
		const uint32_t batchSize    = random.NextInRange(32, 512);

		std::vector<uint64_t> partialSums(jobSystem.GetExecutorCount(), 0);
		uint64_t*             partialSumsData = partialSums.data();

		fang::ParallelFor(
			jobSystem,
			0,
			elementCount,
			batchSize,
			[partialSumsData](uint32_t index, uint32_t workerIndex) { partialSumsData[workerIndex] += index; }
		);

		uint64_t parallelSum = 0;
		for (const uint64_t partialSum : partialSums)
		{
			parallelSum += partialSum;
		}

		const uint64_t expectedParallelSum =
			elementCount == 0 ? 0 : static_cast<uint64_t>(elementCount) * (elementCount - 1) / 2;
		if (parallelSum != expectedParallelSum)
		{
			++wrongParallelCount;
		}
	}

	CHECK(wrongSumCount == 0);
	CHECK(wrongOrderCount == 0);
	CHECK(wrongParallelCount == 0);

	jobSystem.Shutdown();
}


TEST_CASE("作っては畳むのを繰り返してもスレッドが残らない")
{
	uint32_t leftoverThreadCount = 0;
	uint32_t wrongSumCount       = 0;

	for (uint32_t iteration = 0; iteration < 64; ++iteration)
	{
		fang::JobSystem jobSystem;
		if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 1 + iteration % 8 }))
		{
			CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
			return;
		}

		std::atomic<uint64_t> sum{ 0 };

		fang::JobDesc desc{};
		desc.function     = &RunStressJob;
		desc.argumentSize = sizeof(StressJobArguments);

		fang::JobCounter counter;
		for (uint32_t i = 0; i < 256; ++i)
		{
			const StressJobArguments jobArguments{ &sum, 1, 0 };
			desc.arguments = &jobArguments;
			jobSystem.Submit(desc, &counter);
		}

		jobSystem.Wait(counter);
		jobSystem.Shutdown();

		leftoverThreadCount += jobSystem.GetRunningThreadCount();
		if (sum.load() != 256)
		{
			++wrongSumCount;
		}
	}

	CHECK(leftoverThreadCount == 0);
	CHECK(wrongSumCount == 0);
}
