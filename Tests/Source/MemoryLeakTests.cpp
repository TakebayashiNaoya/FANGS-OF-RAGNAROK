/**
 * @file MemoryLeakTests.cpp
 * @brief リーク検出のテスト。追跡を入れた構成でだけ通るものと、どちらの構成でも通るものを分ける。
 */
#include "Core/Memory/Allocator.h"
#include "Core/Memory/HeapAllocator.h"
#include "Core/Memory/New.h"
#include <doctest.h>
#include <cstdint>


TEST_CASE("ReportLeaks はまだ返っていない確保の件数を返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("リーク件数");

	CHECK(heap.ReportLeaks() == 0);

	int* first = new (heap) int(1);
	CHECK(heap.ReportLeaks() == 1);

	int* second = new (heap) int(2);
	CHECK(heap.ReportLeaks() == 2);

	delete first;
	CHECK(heap.ReportLeaks() == 1);

	delete second;
	CHECK(heap.ReportLeaks() == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("確保して解放すればリークは 0 件")
{
	fang::HeapAllocator& heap = fang::CreateHeap("リーク無し");

	void* memory = heap.Allocate(128, 64);
	CHECK(memory != nullptr);
	heap.Deallocate(memory);

	CHECK(heap.ReportLeaks() == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("前置きの大きさは追跡記録の有無で変わる")
{
#if FANG_ENABLE_MEMORY_TRACKING
	// 確保ヘッダ 16 バイトと追跡記録 40 バイトの計 56 バイトを境界へ切り上げる。
	CHECK(fang::GetAllocationPrefixSize(16) == 64);
	CHECK(fang::GetAllocationPrefixSize(64) == 64);
	CHECK(fang::GetAllocationPrefixSize(4096) == 4096);
#else
	CHECK(fang::GetAllocationPrefixSize(16) == 16);
	CHECK(fang::GetAllocationPrefixSize(64) == 64);
	CHECK(fang::GetAllocationPrefixSize(4096) == 4096);
#endif
}


#if FANG_ENABLE_MEMORY_TRACKING

TEST_CASE("追跡記録は利用者ポインタの 56 バイト手前にある")
{
	fang::HeapAllocator& heap = fang::CreateHeap("記録の位置");

	// 境界を変えても伸び縮みするのは詰め物の側だけなので、記録の位置は変わらない。
	const size_t alignments[] = { 16, 64, 4096 };
	for (const size_t alignment : alignments)
	{
		void* memory = heap.Allocate(32, alignment);
		CHECK(memory != nullptr);

		const fang::AllocationRecord* record = fang::FindAllocationRecord(memory);
		CHECK(record != nullptr);
		CHECK(reinterpret_cast<const unsigned char*>(record) + 56 == static_cast<const unsigned char*>(memory));

		heap.Deallocate(memory);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("new (allocator) は呼び出し元のファイルと行を控える")
{
	fang::HeapAllocator& heap = fang::CreateHeap("呼び出し元");

	const uint32_t expectedLine = static_cast<uint32_t>(__LINE__) + 1;
	int*           value        = new (heap) int(5);

	const fang::AllocationRecord* record = fang::FindAllocationRecord(value);
	CHECK(record != nullptr);
	if (record != nullptr)
	{
		CHECK(record->fileName != nullptr);
		CHECK(record->line == expectedLine);
		CHECK(record->returnAddress == nullptr);
		CHECK(record->serialNumber == 1);
	}

	delete value;
	fang::DestroyHeap(heap);
}


TEST_CASE("裸の new は戻り番地だけを控える")
{
	int* value = new int(5);

	const fang::AllocationRecord* record = fang::FindAllocationRecord(value);
	CHECK(record != nullptr);
	if (record != nullptr)
	{
		CHECK(record->fileName == nullptr);
		CHECK(record->line == 0);
		CHECK(record->returnAddress != nullptr);
	}

	delete value;
}


TEST_CASE("追跡を入れた確保には追跡記録の印が付く")
{
	fang::HeapAllocator& heap = fang::CreateHeap("印");

	void* memory = heap.Allocate(64);
	CHECK(memory != nullptr);

	const fang::AllocationHeader& header = fang::ReadAllocationHeader(memory);
	CHECK(header.magic == fang::ALLOCATION_HEADER_MAGIC_TRACKED);
	CHECK(fang::IsAllocationHeaderMagic(header.magic));

	heap.Deallocate(memory);
	fang::DestroyHeap(heap);
}

#endif
