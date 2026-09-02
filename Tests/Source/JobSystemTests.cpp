/**
 * @file JobSystemTests.cpp
 * @brief ジョブシステムのテスト。実行の分散、カウンタ依存、待ち、後始末。
 */
#include "Core/Job/JobCounter.h"
#include "Core/Job/JobSystem.h"
#include "Core/Job/ParallelFor.h"
#include "Core/Platform/Thread.h"
#include <doctest.h>
#include <atomic>
#include <vector>


namespace
{
	/** @brief 実行したワーカーを記録するジョブの引数。 */
	struct RecordJobArguments
	{
		std::atomic<uint32_t>* executionCountsPerWorker;
		std::atomic<uint32_t>* totalExecutionCount;
		uint32_t               busyLoopCount;
	};


	void RunRecordJob(void* arguments, uint32_t workerIndex)
	{
		const auto& jobArguments = *static_cast<const RecordJobArguments*>(arguments);

		// スティールが起きる程度に、1 件あたり少しだけ時間を使う。
		uint32_t accumulator = 0;
		for (uint32_t i = 0; i < jobArguments.busyLoopCount; ++i)
		{
			accumulator += i;
		}

		if (accumulator != 0xFFFFFFFFu)
		{
			jobArguments.executionCountsPerWorker[workerIndex].fetch_add(1, std::memory_order_relaxed);
			jobArguments.totalExecutionCount->fetch_add(1, std::memory_order_relaxed);
		}
	}


	/** @brief 前段の完了を確かめる後段のジョブの引数。 */
	struct ObserveJobArguments
	{
		std::atomic<uint32_t>* firstStageDoneCount;
		std::atomic<uint32_t>* observedFirstStageDoneCount;
	};


	void RunObserveJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto& jobArguments = *static_cast<const ObserveJobArguments*>(arguments);
		jobArguments.observedFirstStageDoneCount->store(
			jobArguments.firstStageDoneCount->load(std::memory_order_acquire),
			std::memory_order_release
		);
	}


	/** @brief 呼ばれた回数だけを数えるジョブの引数。 */
	struct IncrementJobArguments
	{
		std::atomic<uint32_t>* counter;
	};


	void RunIncrementJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto& jobArguments = *static_cast<const IncrementJobArguments*>(arguments);
		jobArguments.counter->fetch_add(1, std::memory_order_relaxed);
	}
} // namespace


TEST_CASE("ワーカー数を指定しなければ物理コア数から 1 本引いた数になる")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{}))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	const uint32_t coreCount = fang::GetPhysicalCoreCount();
	CHECK(coreCount >= 1);
	CHECK(jobSystem.GetWorkerCount() == (coreCount > 1 ? coreCount - 1 : 1));
	CHECK(jobSystem.GetExecutorCount() == jobSystem.GetWorkerCount() + 1);

	jobSystem.Shutdown();
}


TEST_CASE("ジョブが 1 件も無い状態で待ってもデッドロックしない")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 4 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	fang::JobCounter counter;
	jobSystem.Wait(counter);

	CHECK(counter.IsComplete());

	// 一度使ったカウンタでもう一度積んで待てる。
	std::atomic<uint32_t> executionCount{ 0 };
	IncrementJobArguments jobArguments{ &executionCount };

	fang::JobDesc desc{};
	desc.function     = &RunIncrementJob;
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);
	jobSystem.Submit(desc, &counter);

	jobSystem.Wait(counter);
	CHECK(executionCount.load() == 1);

	jobSystem.Shutdown();
}


TEST_CASE("Shutdown するとワーカースレッドが 1 本も残らない")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 6 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	std::atomic<uint32_t> executionCount{ 0 };
	IncrementJobArguments jobArguments{ &executionCount };

	fang::JobDesc desc{};
	desc.function     = &RunIncrementJob;
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);

	fang::JobCounter counter;
	for (uint32_t i = 0; i < 512; ++i)
	{
		jobSystem.Submit(desc, &counter);
	}

	jobSystem.Wait(counter);
	CHECK(executionCount.load() == 512);

	jobSystem.Shutdown();

	CHECK(jobSystem.GetRunningThreadCount() == 0);
	CHECK(jobSystem.GetWorkerCount() == 0);

	// 畳んだ後に作り直せる。
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 2 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}
	CHECK(jobSystem.GetWorkerCount() == 2);
	jobSystem.Shutdown();
	CHECK(jobSystem.GetRunningThreadCount() == 0);
}


TEST_CASE("待っているスレッドも他のジョブを実行する")
{
	// ワーカー 1 本だけにすると、積んだ側が働かない限り待ち時間が伸びる。
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 1 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	const uint32_t executorCount = jobSystem.GetExecutorCount();

	std::vector<std::atomic<uint32_t>> executionCountsPerWorker(executorCount);
	for (auto& executionCount : executionCountsPerWorker)
	{
		executionCount.store(0, std::memory_order_relaxed);
	}

	std::atomic<uint32_t> totalExecutionCount{ 0 };
	RecordJobArguments    jobArguments{ executionCountsPerWorker.data(), &totalExecutionCount, 4096 };

	fang::JobDesc desc{};
	desc.function     = &RunRecordJob;
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);

	fang::JobCounter counter;
	for (uint32_t i = 0; i < 1024; ++i)
	{
		jobSystem.Submit(desc, &counter);
	}

	jobSystem.Wait(counter);

	CHECK(totalExecutionCount.load() == 1024);
	CHECK(executionCountsPerWorker[fang::JobSystem::MAIN_WORKER_INDEX].load() > 0);

	jobSystem.Shutdown();
}


TEST_CASE("1 万件を積むと全ワーカーが実行に参加する")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 8 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	const uint32_t executorCount = jobSystem.GetExecutorCount();

	std::vector<std::atomic<uint32_t>> executionCountsPerWorker(executorCount);
	for (auto& executionCount : executionCountsPerWorker)
	{
		executionCount.store(0, std::memory_order_relaxed);
	}

	std::atomic<uint32_t> totalExecutionCount{ 0 };
	RecordJobArguments    jobArguments{ executionCountsPerWorker.data(), &totalExecutionCount, 2048 };

	fang::JobDesc desc{};
	desc.function     = &RunRecordJob;
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);

	// プールは 8,192 件なので、1 万件は波に分けて積む。
	// 全員が働くまでは波を足す。眠っていたワーカーが起きるのに数波かかることがあるため。
	constexpr uint32_t JOBS_PER_WAVE      = 2000;
	constexpr uint32_t MINIMUM_WAVE_COUNT = 5;
	constexpr uint32_t MAX_WAVE_COUNT     = 50;

	uint32_t idleWorkerCount = executorCount;
	for (uint32_t wave = 0; wave < MAX_WAVE_COUNT && (wave < MINIMUM_WAVE_COUNT || idleWorkerCount > 0); ++wave)
	{
		fang::JobCounter counter;
		for (uint32_t i = 0; i < JOBS_PER_WAVE; ++i)
		{
			jobSystem.Submit(desc, &counter);
		}

		jobSystem.Wait(counter);

		idleWorkerCount = 0;
		for (const auto& executionCount : executionCountsPerWorker)
		{
			if (executionCount.load(std::memory_order_relaxed) == 0)
			{
				++idleWorkerCount;
			}
		}
	}

	CHECK(totalExecutionCount.load() >= 10000);
	CHECK(idleWorkerCount == 0);

	jobSystem.Shutdown();
}


TEST_CASE("カウンタを挟むと後段は前段の完了後に走る")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 4 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	constexpr uint32_t FIRST_STAGE_JOB_COUNT = 256;

	const uint32_t executorCount = jobSystem.GetExecutorCount();

	std::vector<std::atomic<uint32_t>> executionCountsPerWorker(executorCount);
	for (auto& executionCount : executionCountsPerWorker)
	{
		executionCount.store(0, std::memory_order_relaxed);
	}

	std::atomic<uint32_t> firstStageDoneCount{ 0 };
	std::atomic<uint32_t> observedFirstStageDoneCount{ 0xFFFFFFFFu };

	RecordJobArguments  firstStageArguments{ executionCountsPerWorker.data(), &firstStageDoneCount, 1024 };
	ObserveJobArguments secondStageArguments{ &firstStageDoneCount, &observedFirstStageDoneCount };

	fang::JobCounter firstStageCounter;
	fang::JobCounter secondStageCounter;

	fang::JobDesc firstStageDesc{};
	firstStageDesc.function     = &RunRecordJob;
	firstStageDesc.arguments    = &firstStageArguments;
	firstStageDesc.argumentSize = sizeof(firstStageArguments);

	for (uint32_t i = 0; i < FIRST_STAGE_JOB_COUNT; ++i)
	{
		jobSystem.Submit(firstStageDesc, &firstStageCounter);
	}

	fang::JobDesc secondStageDesc{};
	secondStageDesc.function     = &RunObserveJob;
	secondStageDesc.arguments    = &secondStageArguments;
	secondStageDesc.argumentSize = sizeof(secondStageArguments);
	secondStageDesc.waitCounter  = &firstStageCounter;

	jobSystem.Submit(secondStageDesc, &secondStageCounter);
	jobSystem.Wait(secondStageCounter);

	CHECK(firstStageDoneCount.load() == FIRST_STAGE_JOB_COUNT);
	CHECK(observedFirstStageDoneCount.load() == FIRST_STAGE_JOB_COUNT);

	jobSystem.Shutdown();
}


TEST_CASE("依存の解けているカウンタに積んだジョブもちゃんと走る")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 2 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	std::atomic<uint32_t> executionCount{ 0 };
	IncrementJobArguments jobArguments{ &executionCount };

	// 一度も Submit していないカウンタは 0 なので、待たずに実行されるのが正しい。
	fang::JobCounter emptyCounter;
	fang::JobCounter finishCounter;

	fang::JobDesc desc{};
	desc.function     = &RunIncrementJob;
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);
	desc.waitCounter  = &emptyCounter;

	for (uint32_t i = 0; i < 128; ++i)
	{
		jobSystem.Submit(desc, &finishCounter);
	}

	jobSystem.Wait(finishCounter);
	CHECK(executionCount.load() == 128);

	jobSystem.Shutdown();
}


#if FANG_ENABLE_PROFILER

TEST_CASE("待ち終わると使用中のジョブは 0 に戻り、高水位だけが残る")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 4 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	CHECK(jobSystem.GetJobsInUseCount() == 0);
	CHECK(jobSystem.GetPeakJobsInUseCount() == 0);
	CHECK(jobSystem.GetInlineExecutedJobCount() == 0);

	constexpr uint32_t JOB_COUNT = 1024;

	std::atomic<uint32_t> executionCount{ 0 };
	IncrementJobArguments jobArguments{ &executionCount };

	fang::JobDesc desc{};
	desc.function     = &RunIncrementJob;
	desc.arguments    = &jobArguments;
	desc.argumentSize = sizeof(jobArguments);

	fang::JobCounter counter;
	for (uint32_t i = 0; i < JOB_COUNT; ++i)
	{
		jobSystem.Submit(desc, &counter);
	}

	jobSystem.Wait(counter);

	CHECK(executionCount.load() == JOB_COUNT);
	CHECK(jobSystem.GetJobsInUseCount() == 0);
	CHECK(jobSystem.GetPeakJobsInUseCount() >= 1);
	CHECK(jobSystem.GetPeakJobsInUseCount() <= fang::JobSystem::JOB_POOL_CAPACITY);
	CHECK(jobSystem.GetInlineExecutedJobCount() == 0);

	// リセットは今の使用数まで戻すので、待ち終わった後なら 0 になる。
	jobSystem.ResetPeakJobsInUseCount();
	CHECK(jobSystem.GetPeakJobsInUseCount() == 0);

	jobSystem.Shutdown();

	// 畳んだ後でも統計は読める。
	CHECK(jobSystem.GetJobsInUseCount() == 0);
	CHECK(jobSystem.GetPeakJobsInUseCount() == 0);
	CHECK(jobSystem.GetInlineExecutedJobCount() == 0);
}


TEST_CASE("ParallelFor を回してもプールは溢れず、その場実行に縮退しない")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 4 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	constexpr uint32_t ELEMENT_COUNT = 65536;
	constexpr uint32_t BATCH_SIZE    = 256;

	std::vector<uint32_t> values(ELEMENT_COUNT);
	for (uint32_t i = 0; i < ELEMENT_COUNT; ++i)
	{
		values[i] = i;
	}

	// ワーカー番号を名乗るスレッドは 1 本だけなので、部分和の加算に同期は要らない。
	std::vector<uint64_t> partialSums(fang::JobSystem::MAX_WORKER_COUNT + 1, 0);

	uint32_t* const valueArray      = values.data();
	uint64_t* const partialSumArray = partialSums.data();

	fang::ParallelFor(
		jobSystem,
		0,
		ELEMENT_COUNT,
		BATCH_SIZE,
		[valueArray, partialSumArray](uint32_t index, uint32_t workerIndex) {
			partialSumArray[workerIndex] += valueArray[index];
		}
	);

	uint64_t totalSum = 0;
	for (const uint64_t partialSum : partialSums)
	{
		totalSum += partialSum;
	}

	CHECK(totalSum == static_cast<uint64_t>(ELEMENT_COUNT) * (ELEMENT_COUNT - 1) / 2);
	CHECK(jobSystem.GetJobsInUseCount() == 0);
	CHECK(jobSystem.GetPeakJobsInUseCount() <= fang::JobSystem::JOB_POOL_CAPACITY);
	CHECK(jobSystem.GetInlineExecutedJobCount() == 0);

	jobSystem.Shutdown();
}

#endif
