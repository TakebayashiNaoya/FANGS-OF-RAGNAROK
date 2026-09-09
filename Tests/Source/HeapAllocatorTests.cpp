/**
 * @file HeapAllocatorTests.cpp
 * @brief 汎用ヒープのテスト。境界の守り方と、統計の増減を確かめる。
 */
#include "Core/Memory/HeapAllocator.h"
#include <doctest.h>
#include <cstdint>
#include <string_view>


namespace
{
	/** @brief ForEachHeap に渡す、名前で探すための入れ物。 */
	struct HeapSearch
	{
		const char* name;
		bool        isFound;
	};

	/** @brief 名前が一致するヒープが回ってきたら印を付ける。 */
	void FindHeapByName(fang::HeapAllocator& heap, void* userData)
	{
		HeapSearch* search = static_cast<HeapSearch*>(userData);
		if (std::string_view(heap.GetName()) == search->name)
		{
			search->isFound = true;
		}
	}
} // namespace


TEST_CASE("頼んだ境界に載ったポインタが返る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("境界");

	const size_t alignments[] = { 16, 32, 64, 256, 4096 };
	for (const size_t alignment : alignments)
	{
		void* memory = heap.Allocate(100, alignment);
		CHECK(memory != nullptr);
		CHECK((reinterpret_cast<uintptr_t>(memory) % alignment) == 0);
		heap.Deallocate(memory);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("確保ヘッダは利用者ポインタの直前にある")
{
	fang::HeapAllocator& heap = fang::CreateHeap("ヘッダ");

	void* memory = heap.Allocate(48, 64);
	CHECK(memory != nullptr);

	const fang::AllocationHeader& header = fang::ReadAllocationHeader(memory);
	CHECK(header.allocator == &heap);
	CHECK(header.size == 48);
	CHECK(header.magic == fang::ALLOCATION_HEADER_MAGIC);

	// 境界 64 では前置きが 64 まで伸びる。ヘッダはその末尾 16 バイトを使う。
	CHECK(header.offsetToBlock == 64);

	heap.Deallocate(memory);
	fang::DestroyHeap(heap);
}


TEST_CASE("境界 16 では詰め物が出ない")
{
	CHECK(fang::GetAllocationPrefixSize(16) == 16);
	CHECK(fang::GetAllocationPrefixSize(32) == 32);
	CHECK(fang::GetAllocationPrefixSize(64) == 64);
}


TEST_CASE("統計は確保で増えて解放で戻る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("統計");

	CHECK(heap.GetStatistics().usedBytes == 0);
	CHECK(heap.GetStatistics().totalAllocationCount == 0);

	void* first = heap.Allocate(1000);
	CHECK(heap.GetStatistics().usedBytes == 1000);
	CHECK(heap.GetStatistics().liveAllocationCount == 1);

	void* second = heap.Allocate(500);
	CHECK(heap.GetStatistics().usedBytes == 1500);
	CHECK(heap.GetStatistics().peakBytes == 1500);

	heap.Deallocate(second);
	heap.Deallocate(first);

	const fang::AllocatorStatistics statistics = heap.GetStatistics();
	CHECK(statistics.usedBytes == 0);
	CHECK(statistics.liveAllocationCount == 0);

	// 最高水位と累計は戻らない。減った後の山の高さを覚えておくため。
	CHECK(statistics.peakBytes == 1500);
	CHECK(statistics.totalAllocationCount == 2);

	fang::DestroyHeap(heap);
}


TEST_CASE("nullptr の解放は何もしない")
{
	fang::HeapAllocator& heap = fang::CreateHeap("空");

	heap.Deallocate(nullptr);
	CHECK(heap.GetStatistics().liveAllocationCount == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("取り出した領域は最後まで書ける")
{
	fang::HeapAllocator& heap = fang::CreateHeap("書き込み");

	constexpr size_t count = 777;
	unsigned char*   bytes = static_cast<unsigned char*>(heap.Allocate(count, 32));
	CHECK(bytes != nullptr);

	for (size_t index = 0; index < count; ++index)
	{
		bytes[index] = static_cast<unsigned char>(index & 0xFF);
	}

	CHECK(bytes[0] == 0);
	CHECK(bytes[count - 1] == static_cast<unsigned char>((count - 1) & 0xFF));

	heap.Deallocate(bytes);
	fang::DestroyHeap(heap);
}


TEST_CASE("既定のヒープは名前を持つ")
{
	CHECK(std::string_view(fang::HeapAllocator::GetInstance().GetName()) == "Default");
}


TEST_CASE("名前付きヒープは渡した名前を返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("名前");

	CHECK(std::string_view(heap.GetName()) == "名前");

	fang::DestroyHeap(heap);
}


TEST_CASE("名前付きヒープの実体は既定のヒープから取る")
{
	const fang::HeapAllocator&      defaultHeap = fang::HeapAllocator::GetInstance();
	const fang::AllocatorStatistics before      = defaultHeap.GetStatistics();

	fang::HeapAllocator& heap = fang::CreateHeap("実体");
	CHECK(defaultHeap.GetStatistics().totalAllocationCount > before.totalAllocationCount);
	CHECK(defaultHeap.GetStatistics().liveAllocationCount == before.liveAllocationCount + 1);

	fang::DestroyHeap(heap);
	CHECK(defaultHeap.GetStatistics().liveAllocationCount == before.liveAllocationCount);
}


TEST_CASE("ForEachHeap は作ったヒープを見つけ、壊した後は見つけない")
{
	HeapSearch search{ "巡回", false };
	fang::ForEachHeap(FindHeapByName, &search);
	CHECK(search.isFound == false);

	fang::HeapAllocator& heap = fang::CreateHeap("巡回");

	search.isFound = false;
	fang::ForEachHeap(FindHeapByName, &search);
	CHECK(search.isFound);

	fang::DestroyHeap(heap);

	search.isFound = false;
	fang::ForEachHeap(FindHeapByName, &search);
	CHECK(search.isFound == false);
}
