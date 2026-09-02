/**
 * @file FrameAllocatorTests.cpp
 * @brief フレームアロケータのテスト。リニア確保とリセット、アラインメント、2 枚の切り替え、並列確保。
 */
#include "Core/CoreMacros.h"
#include "Core/Job/JobSystem.h"
#include "Core/Job/ParallelFor.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include <doctest.h>
#include <algorithm>
#include <cstdint>
#include <vector>


namespace
{
	/** @brief ジョブ 1 本が 1 回で取るバイト数。16 の倍数なので切り上げの端数が出ない。 */
	constexpr size_t PARALLEL_BLOCK_SIZE = 64;

	/** @brief 並列確保で配るブロックの数。 */
	constexpr uint32_t PARALLEL_BLOCK_COUNT = 2048;

	/** @brief ブロック 1 つに書き込む uint32_t の数。 */
	constexpr uint32_t PARALLEL_BLOCK_WORD_COUNT = static_cast<uint32_t>(PARALLEL_BLOCK_SIZE / sizeof(uint32_t));


	/**
	 * @brief 配られたブロックが重なっておらず、書いた値がそのまま残っているかを確かめる。
	 * @details 重なりはアドレス順に並べ替えて隣との距離で見る。値は「自分の番号で埋める」約束なので、
	 *          別のジョブに踏まれていれば違う番号が残る。
	 */
	bool AreBlocksDisjointAndIntact(const std::vector<uint32_t*>& blocks)
	{
		std::vector<uint32_t*> sortedBlocks = blocks;
		std::sort(sortedBlocks.begin(), sortedBlocks.end());

		for (size_t i = 0; i < sortedBlocks.size(); ++i)
		{
			if (sortedBlocks[i] == nullptr)
			{
				return false;
			}

			if (i == 0)
			{
				continue;
			}

			const auto* previousBytes = reinterpret_cast<const unsigned char*>(sortedBlocks[i - 1]);
			const auto* currentBytes  = reinterpret_cast<const unsigned char*>(sortedBlocks[i]);
			if (static_cast<size_t>(currentBytes - previousBytes) < PARALLEL_BLOCK_SIZE)
			{
				return false;
			}
		}

		for (size_t index = 0; index < blocks.size(); ++index)
		{
			for (uint32_t word = 0; word < PARALLEL_BLOCK_WORD_COUNT; ++word)
			{
				if (blocks[index][word] != static_cast<uint32_t>(index))
				{
					return false;
				}
			}
		}

		return true;
	}
} // namespace


TEST_CASE("確保はリニアに並び、リセットでアドレスが巻き戻る")
{
	fang::FrameAllocator allocator;
	if (!allocator.Initialize(fang::HeapAllocator::GetInstance(), 4096, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	auto* first  = static_cast<unsigned char*>(allocator.Allocate(64));
	auto* second = static_cast<unsigned char*>(allocator.Allocate(64));

	CHECK(first != nullptr);
	CHECK(second != nullptr);
	if (first == nullptr || second == nullptr)
	{
		allocator.Shutdown();
		return;
	}

	CHECK(second == first + 64);
	CHECK(allocator.GetUsedBytes() == 128);

	// 要求を 16 の倍数へ切り上げるので、1 バイトずつ取っても次は 16 先から始まる。
	auto* third  = static_cast<unsigned char*>(allocator.Allocate(1));
	auto* fourth = static_cast<unsigned char*>(allocator.Allocate(1));
	CHECK(third != nullptr);
	CHECK(fourth != nullptr);
	if (third != nullptr && fourth != nullptr)
	{
		CHECK(fourth == third + 16);
	}

	allocator.Reset();
	CHECK(allocator.GetUsedBytes() == 0);
	CHECK(allocator.Allocate(64) == first);

	allocator.Shutdown();
}


TEST_CASE("16 / 64 / 256 のアラインメント指定が守られる")
{
	fang::FrameAllocator allocator;
	if (!allocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	constexpr size_t alignments[] = { 16, 64, 256 };
	for (const size_t alignment : alignments)
	{
		for (uint32_t i = 0; i < 8; ++i)
		{
			// 間に端数を挟んで、境界がたまたま揃っただけにならないようにする。
			(void)allocator.Allocate(1);

			void* block = allocator.Allocate(48, alignment);
			CHECK(block != nullptr);
			if (block != nullptr)
			{
				CHECK((reinterpret_cast<uintptr_t>(block) % alignment) == 0);
			}
		}
	}

	allocator.Shutdown();
}


TEST_CASE("2 枚の切り替えで前のフレームのデータが読める")
{
	fang::FrameMemory frameMemory;
	if (!frameMemory.Initialize(fang::FrameMemoryDesc{ .capacityPerBuffer = 64 * 1024 }))
	{
		CHECK_MESSAGE(false, "フレームメモリを初期化できなかった");
		return;
	}

	CHECK(frameMemory.GetCapacityPerBuffer() == 64 * 1024);

	frameMemory.BeginFrame();
	uint32_t* firstFrameValue = fang::NewFrame<uint32_t>(frameMemory.GetCurrent(), 0xFEEDFACEu);
	CHECK(firstFrameValue != nullptr);
	if (firstFrameValue == nullptr)
	{
		frameMemory.Shutdown();
		return;
	}

	// 切り替えても前のフレームの側はリセットされないので、書いた値がそのまま読める。
	frameMemory.BeginFrame();
	CHECK(*firstFrameValue == 0xFEEDFACEu);
	CHECK(frameMemory.GetPrevious().GetUsedBytes() > 0);
	CHECK(frameMemory.GetCurrent().GetUsedBytes() == 0);

	uint32_t* secondFrameValue = fang::NewFrame<uint32_t>(frameMemory.GetCurrent(), 0x12345678u);
	CHECK(secondFrameValue != nullptr);
	CHECK(secondFrameValue != firstFrameValue);
	CHECK(*firstFrameValue == 0xFEEDFACEu);

	// もう一度切り替えると 1 枚目が今のフレームに戻り、同じ番地から配り直される。
	frameMemory.BeginFrame();
	CHECK(frameMemory.GetCurrent().GetUsedBytes() == 0);
	CHECK(fang::NewFrame<uint32_t>(frameMemory.GetCurrent(), 0u) == firstFrameValue);

	frameMemory.Shutdown();
}


TEST_CASE("NewFrame は自明に壊せる型を確保して構築する")
{
	struct Payload
	{
		uint32_t identifier;
		float    weight;
	};

	fang::FrameAllocator allocator;
	if (!allocator.Initialize(fang::HeapAllocator::GetInstance(), 4096, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	Payload* payload = fang::NewFrame<Payload>(allocator, 7u, 1.5f);
	CHECK(payload != nullptr);
	if (payload != nullptr)
	{
		CHECK(payload->identifier == 7u);
		CHECK(payload->weight == 1.5f);
	}

	// デストラクタを持つ型を NewFrame へ渡すと static_assert でビルドが止まるので、通るテストには書けない。
	// 確かめたいときは Payload にデストラクタを足し、この TU がコンパイルできなくなることを見る。

	allocator.Shutdown();
}


TEST_CASE("ジョブ 8 本から同時に確保しても領域が重ならず、こぼれない")
{
	fang::JobSystem jobSystem;
	if (!jobSystem.Initialize(fang::JobSystemDesc{ .workerCount = 8 }))
	{
		CHECK_MESSAGE(false, "ジョブシステムを開始できなかった");
		return;
	}

	// ちょうど配り切れる容量にして、端数が出ないことも一緒に確かめる。
	constexpr size_t capacity = PARALLEL_BLOCK_SIZE * PARALLEL_BLOCK_COUNT;

	fang::FrameAllocator allocator;
	if (!allocator.Initialize(fang::HeapAllocator::GetInstance(), capacity, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		jobSystem.Shutdown();
		return;
	}

	std::vector<uint32_t*> blocks(PARALLEL_BLOCK_COUNT, nullptr);

	fang::FrameAllocator* allocatorPointer = &allocator;
	uint32_t**            blocksData       = blocks.data();

	// 1 回では重なりを踏み損ねるので、リセットを挟んで何度も回す。
	constexpr uint32_t roundCount       = 64;
	uint32_t           failedRoundCount = 0;
	for (uint32_t round = 0; round < roundCount; ++round)
	{
		allocator.Reset();
		std::fill(blocks.begin(), blocks.end(), nullptr);

		fang::ParallelFor(
			jobSystem,
			0,
			PARALLEL_BLOCK_COUNT,
			8,
			[allocatorPointer, blocksData](uint32_t index, uint32_t workerIndex) {
				FANG_UNUSED(workerIndex);

				auto* block = static_cast<uint32_t*>(allocatorPointer->Allocate(PARALLEL_BLOCK_SIZE));
				if (block != nullptr)
				{
					// 自分の番号で埋める。別のジョブと重なっていれば、後から読んだときに違う番号が残る。
					for (uint32_t word = 0; word < PARALLEL_BLOCK_WORD_COUNT; ++word)
					{
						block[word] = index;
					}
				}

				blocksData[index] = block;
			}
		);

		if (!AreBlocksDisjointAndIntact(blocks))
		{
			++failedRoundCount;
		}

		// こぼれや取りすぎがあれば、進んだ量が容量ちょうどにならない。
		if (allocator.GetUsedBytes() != capacity)
		{
			++failedRoundCount;
		}
	}

	CHECK(failedRoundCount == 0);

	allocator.Shutdown();
	jobSystem.Shutdown();
}


#if FANG_ENABLE_PROFILER

TEST_CASE("高水位と確保回数がリセットをまたいで取れる")
{
	fang::FrameAllocator allocator;
	if (!allocator.Initialize(fang::HeapAllocator::GetInstance(), 4096, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	CHECK(allocator.Allocate(64) != nullptr);
	CHECK(allocator.Allocate(64) != nullptr);
	CHECK(allocator.Allocate(64) != nullptr);
	CHECK(allocator.GetAllocationCount() == 3);

	allocator.Reset();
	CHECK(allocator.GetAllocationCount() == 0);
	CHECK(allocator.GetPeakUsedBytes() == 192);

	// 高水位は一番使ったフレームの値のまま残る。
	CHECK(allocator.Allocate(64) != nullptr);
	allocator.Reset();
	CHECK(allocator.GetPeakUsedBytes() == 192);

	allocator.Shutdown();
}

#endif


#if !FANG_ENABLE_ASSERT

TEST_CASE("容量を超えたら nullptr が返る")
{
	// アサートを有効にした構成では同じ経路が停止するので、この確認はアサートを外した構成だけで行う。
	fang::FrameAllocator allocator;
	if (!allocator.Initialize(fang::HeapAllocator::GetInstance(), 256, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	CHECK(allocator.Allocate(128) != nullptr);
	CHECK(allocator.Allocate(128) != nullptr);
	CHECK(allocator.Allocate(16) == nullptr);

	// 1 件で容量を超える要求も nullptr。
	allocator.Reset();
	CHECK(allocator.Allocate(1024) == nullptr);

	// 満杯を踏んだ後でも、リセットすればまた配れる。
	allocator.Reset();
	CHECK(allocator.Allocate(128) != nullptr);

	allocator.Shutdown();
}

#endif
