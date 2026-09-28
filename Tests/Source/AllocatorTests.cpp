/**
 * @file AllocatorTests.cpp
 * @brief アロケータの共通の約束のテスト。
 */
#include "Core/Memory/Allocator.h"
#include "Core/Memory/HeapAllocator.h"
#include <doctest.h>


TEST_CASE("境界 16 未満は 16 に切り上げる")
{
	const fang::HeapAllocator& heap = fang::HeapAllocator::GetInstance();

	const size_t alignments[] = { 1, 2, 4, 8 };
	for (const size_t alignment : alignments)
	{
		CHECK(fang::ValidateAllocationRequest(heap, 8, alignment) == fang::IAllocator::DEFAULT_ALIGNMENT);
	}
}


TEST_CASE("境界 16 以上はそのまま使う")
{
	const fang::HeapAllocator& heap = fang::HeapAllocator::GetInstance();

	const size_t alignments[] = { 16, 32, 64, 4096, fang::MAXIMUM_ALLOCATION_ALIGNMENT };
	for (const size_t alignment : alignments)
	{
		CHECK(fang::ValidateAllocationRequest(heap, 8, alignment) == alignment);
	}
}


TEST_CASE("大きさは上限ちょうどまで受け付ける")
{
	const fang::HeapAllocator& heap = fang::HeapAllocator::GetInstance();

	// 検査だけで確保はしない。
	CHECK(fang::ValidateAllocationRequest(heap, fang::MAXIMUM_ALLOCATION_SIZE, 16) == 16);
}
