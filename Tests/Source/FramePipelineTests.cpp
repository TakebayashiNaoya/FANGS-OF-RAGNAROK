/**
 * @file FramePipelineTests.cpp
 * @brief フレームパイプラインのテスト。更新と描画の 1 フレームのずれ、面の入れ替え、ワーカー数と縮退。
 */
#include "Core/CoreMacros.h"
#include "Core/Job/JobCounter.h"
#include "Core/Job/JobSystem.h"
#include "Core/Memory/FrameAllocator.h"
#include "Runtime/FrameContext.h"
#include "Runtime/FramePipeline.h"
#include <doctest.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>


namespace
{
	/** @brief 回す周の数。 */
	constexpr uint32_t FRAME_COUNT = 100;

	/** @brief 1 枚あたりのフレームメモリ。偽の更新は 1 周に 1 個しか置かない。 */
	constexpr size_t FRAME_MEMORY_CAPACITY = 64 * 1024;

	/** @brief 偽の 1 周に渡す経過時間。値そのものは見ない。 */
	constexpr float TEST_DELTA_TIME_SECONDS = 1.0f / 60.0f;

	/** @brief 塞ぎ役が待ちきる上限。メインが自分で引いてしまってもテストが止まらないようにするため。 */
	constexpr std::chrono::seconds BLOCKER_TIME_LIMIT{ 5 };

	/** @brief 塞ぎ役をワーカーが拾うのを待つ時間。 */
	constexpr std::chrono::milliseconds BLOCKER_HANDOVER_TIME{ 5 };


	/** @brief 偽の更新が今の面へ置くもの。描画は 1 周遅れでこれを読む。 */
	struct TestFrameData : fang::FrameData
	{
		uint64_t frameIndex = 0;
	};

	/** @brief 塞ぎ役のジョブへ写す引数。 */
	struct BlockerJobArguments
	{
		std::atomic<bool>* isReleased = nullptr;
	};

	/** @brief 更新と描画が見たものの記録。更新はフレーム番号で引く枠だけ、ほかはメインだけが触る。 */
	struct FrameRecorder
	{
		std::vector<const void*> updateAddresses;      /**< 更新が確保した番地。面が交互かを見る。 */
		std::vector<uint64_t>    renderedFrameIndices; /**< 描画が受け取った番号の列。 */

		uint32_t staleFrameDataCount = 0; /**< 読んだ中身が自分の番号と食い違った回数。 */
		uint32_t nullFrameDataCount  = 0; /**< 読む相手がいなかった回数。 */

		bool isUpdateComplete = false; /**< 100 周の後、投げたジョブを回収し終えていたか。 */
	};

	/** @brief 偽の更新。今の面へ自分のフレーム番号を置いて返す。 */
	fang::FrameData* UpdateFrame(void* userData, const fang::FrameUpdateContext& context)
	{
		auto& recorder = *static_cast<FrameRecorder*>(userData);

		auto* frameData = fang::NewFrame<TestFrameData>(context.frameAllocator);
		if (frameData == nullptr)
		{
			return nullptr;
		}

		frameData->frameIndex = context.frameIndex;

		recorder.updateAddresses[static_cast<size_t>(context.frameIndex)] = frameData;

		return frameData;
	}

	/** @brief 偽の描画。受け取った番号と、その中身が生きているかを控える。 */
	void RenderFrame(void* userData, const fang::FrameData* frameData, uint64_t frameIndex, float deltaTimeSeconds)
	{
		FANG_UNUSED(deltaTimeSeconds);

		auto& recorder = *static_cast<FrameRecorder*>(userData);
		recorder.renderedFrameIndices.push_back(frameIndex);

		if (frameData == nullptr)
		{
			++recorder.nullFrameDataCount;
			return;
		}

		// 前の面が空けられていれば、書いたはずの番号が残っていない。
		if (static_cast<const TestFrameData*>(frameData)->frameIndex != frameIndex)
		{
			++recorder.staleFrameDataCount;
		}
	}

	/** @brief 解除の合図か期限が来るまで、そのスレッドを塞ぎ続けるジョブ。 */
	void RunBlockerJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto& blockerArguments = *static_cast<const BlockerJobArguments*>(arguments);
		const auto  timeLimitAt      = std::chrono::steady_clock::now() + BLOCKER_TIME_LIMIT;

		while (!blockerArguments.isReleased->load(std::memory_order_acquire) &&
			   std::chrono::steady_clock::now() < timeLimitAt)
		{
			std::this_thread::yield();
		}
	}

	/**
	 * @brief ワーカー数を決めて 100 周回し、記録を残す。
	 * @param isWorkerBlocked 真なら、回している間ワーカーを別の長いジョブで塞ぐ。
	 * @return 途中の初期化に失敗したら false。
	 */
	bool RunFrames(uint32_t workerCount, bool isWorkerBlocked, FrameRecorder& recorder)
	{
		recorder.updateAddresses.assign(FRAME_COUNT + 1, nullptr);
		recorder.renderedFrameIndices.reserve(FRAME_COUNT);

		fang::JobSystem jobSystem;
		if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = workerCount }))
		{
			return false;
		}

		fang::FrameMemory frameMemory;
		if (!frameMemory.Initialize(fang::FrameMemoryDesc{ .capacityPerBuffer = FRAME_MEMORY_CAPACITY }))
		{
			jobSystem.Shutdown();
			return false;
		}

		std::atomic<bool> isBlockerReleased{ false };
		fang::JobCounter  blockerCounter;

		if (isWorkerBlocked)
		{
			const BlockerJobArguments blockerArguments{ &isBlockerReleased };

			fang::JobDesc blockerDesc{};
			blockerDesc.function     = &RunBlockerJob;
			blockerDesc.arguments    = &blockerArguments;
			blockerDesc.argumentSize = sizeof(blockerArguments);
			jobSystem.Submit(blockerDesc, &blockerCounter);

			// ワーカーが先に拾うのを待つ。メインが拾ってしまっても、期限で抜けるので止まりはしない。
			std::this_thread::sleep_for(BLOCKER_HANDOVER_TIME);
		}

		bool isRun = false;

		fang::FramePipeline framePipeline;
		if (framePipeline.Initialize(jobSystem, frameMemory, &recorder, &UpdateFrame, &RenderFrame))
		{
			framePipeline.Prime();
			for (uint32_t i = 0; i < FRAME_COUNT; ++i)
			{
				framePipeline.RunFrame(TEST_DELTA_TIME_SECONDS);
			}

			recorder.isUpdateComplete = framePipeline.IsUpdateComplete();
			framePipeline.Shutdown();

			isRun = true;
		}

		if (isWorkerBlocked)
		{
			isBlockerReleased.store(true, std::memory_order_release);
			jobSystem.Wait(blockerCounter);
		}

		frameMemory.Shutdown();
		jobSystem.Shutdown();

		return isRun;
	}

	/** @brief 描かれた番号が 0 から抜けも重なりもなく続いているか。 */
	bool IsRenderedSequenceContinuous(const std::vector<uint64_t>& renderedFrameIndices)
	{
		if (renderedFrameIndices.size() != FRAME_COUNT)
		{
			return false;
		}

		for (size_t i = 0; i < renderedFrameIndices.size(); ++i)
		{
			if (renderedFrameIndices[i] != i)
			{
				return false;
			}
		}

		return true;
	}

	/** @brief 更新が毎周ちゃんと確保できていたか。 */
	bool HasEveryUpdateAllocated(const std::vector<const void*>& updateAddresses)
	{
		if (updateAddresses.size() != FRAME_COUNT + 1)
		{
			return false;
		}

		for (const void* address : updateAddresses)
		{
			if (address == nullptr)
			{
				return false;
			}
		}

		return true;
	}
} // namespace


TEST_CASE("描画は 1 つ前のフレームの更新が書いたものを、抜けも重なりもなく受け取る")
{
	FrameRecorder recorder;
	if (!RunFrames(2, false, recorder))
	{
		CHECK_MESSAGE(false, "フレームパイプラインを回せなかった");
		return;
	}

	CHECK(IsRenderedSequenceContinuous(recorder.renderedFrameIndices));
	CHECK(recorder.nullFrameDataCount == 0);
	CHECK(recorder.staleFrameDataCount == 0);

	// 助走の 0 番から、描かれずに捨てる最後の 1 周ぶんまで。
	CHECK(HasEveryUpdateAllocated(recorder.updateAddresses));

	// 周の末尾で待つので、ループを抜けた時点で投げっぱなしのジョブは残らない。
	CHECK(recorder.isUpdateComplete);
}


TEST_CASE("更新が書く面は 2 つだけで、周ごとに入れ替わる")
{
	FrameRecorder recorder;
	if (!RunFrames(2, false, recorder))
	{
		CHECK_MESSAGE(false, "フレームパイプラインを回せなかった");
		return;
	}

	const std::vector<const void*>& addresses = recorder.updateAddresses;
	if (addresses.size() < 3)
	{
		CHECK_MESSAGE(false, "面の入れ替わりを見るには周が足りない");
		return;
	}

	uint32_t neighborMatchCount   = 0;
	uint32_t alternationMissCount = 0;
	for (size_t i = 1; i < addresses.size(); ++i)
	{
		if (addresses[i] == addresses[i - 1])
		{
			++neighborMatchCount;
		}

		if (i >= 2 && addresses[i] != addresses[i - 2])
		{
			++alternationMissCount;
		}
	}

	// 隣の周とは別の番地、1 つ飛ばしでは同じ番地。➡ 2 面が交互に回っている。
	CHECK(neighborMatchCount == 0);
	CHECK(alternationMissCount == 0);
}


TEST_CASE("ワーカー数が 1 / 2 / 8 のどれでも描かれる順番が変わらない")
{
	constexpr uint32_t workerCounts[] = { 1, 2, 8 };

	for (const uint32_t workerCount : workerCounts)
	{
		FrameRecorder recorder;
		if (!RunFrames(workerCount, false, recorder))
		{
			CHECK_MESSAGE(false, "フレームパイプラインを回せなかった");
			return;
		}

		CHECK(IsRenderedSequenceContinuous(recorder.renderedFrameIndices));
		CHECK(recorder.nullFrameDataCount == 0);
		CHECK(recorder.staleFrameDataCount == 0);
		CHECK(recorder.isUpdateComplete);
	}
}


TEST_CASE("ワーカーが塞がっていても、メインが更新を引き受けて 100 周進む")
{
	// ワーカー 1 本を長いジョブで塞ぐ。Wait の間はメインも実行に参加するので、そこへ縮退して進む。
	FrameRecorder recorder;
	if (!RunFrames(1, true, recorder))
	{
		CHECK_MESSAGE(false, "フレームパイプラインを回せなかった");
		return;
	}

	CHECK(IsRenderedSequenceContinuous(recorder.renderedFrameIndices));
	CHECK(recorder.nullFrameDataCount == 0);
	CHECK(recorder.staleFrameDataCount == 0);
	CHECK(recorder.isUpdateComplete);
}
