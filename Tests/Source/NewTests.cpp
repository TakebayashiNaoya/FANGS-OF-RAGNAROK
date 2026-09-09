/**
 * @file NewTests.cpp
 * @brief アロケータを名指しする new と、グローバルの置き換えのテスト。
 */
#include "Core/Memory/HeapAllocator.h"
#include "Core/Memory/New.h"
#include <doctest.h>
#include <cstdint>
#include <string>
#include <vector>


namespace
{
	int s_liveCount = 0;

	/** @brief 生き死にを数えるだけの型。 */
	struct Counted
	{
		int value;

		explicit Counted(int initialValue = 0)
			: value(initialValue)
		{
			++s_liveCount;
		}
		~Counted() { --s_liveCount; }

		Counted(const Counted& other)
			: value(other.value)
		{
			++s_liveCount;
		}
		Counted& operator=(const Counted&) = default;
	};

#pragma warning(push)
#pragma warning(disable : 4324) // 整列指定で詰め物が入るのは狙いどおり。

	/** @brief 既定より大きい境界を要る型。 */
	struct alignas(64) WideAligned
	{
		int value;

		explicit WideAligned(int initialValue)
			: value(initialValue)
		{
		}
	};

#pragma warning(pop)
} // namespace


TEST_CASE("new (allocator) T が構築し、delete が同じヒープへ返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("生成");
	s_liveCount               = 0;

	Counted* object = new (heap) Counted(7);
	CHECK(object != nullptr);
	CHECK(object->value == 7);
	CHECK(s_liveCount == 1);
	CHECK(heap.GetStatistics().liveAllocationCount == 1);
	CHECK((reinterpret_cast<uintptr_t>(object) % 16) == 0);

	delete object;

	CHECK(s_liveCount == 0);
	CHECK(heap.GetStatistics().liveAllocationCount == 0);
	CHECK(heap.GetStatistics().usedBytes == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("境界の大きい型は align_val_t 付きの形が選ばれる")
{
	fang::HeapAllocator& heap = fang::CreateHeap("境界の大きい型");

	WideAligned* object = new (heap) WideAligned(9);
	CHECK(object != nullptr);
	CHECK(object->value == 9);
	CHECK((reinterpret_cast<uintptr_t>(object) % 64) == 0);

	delete object;
	CHECK(heap.GetStatistics().liveAllocationCount == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("配列も同じヒープへ返る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("配列");
	s_liveCount               = 0;

	Counted* objects = new (heap) Counted[4];
	CHECK(objects != nullptr);
	CHECK(s_liveCount == 4);

	objects[3].value = 30;
	CHECK(objects[3].value == 30);

	delete[] objects;

	CHECK(s_liveCount == 0);
	CHECK(heap.GetStatistics().liveAllocationCount == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("MakeUnique は所有を std::unique_ptr で持ち、抜けたら返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("所有");
	s_liveCount               = 0;

	{
		std::unique_ptr<Counted> owned = fang::MakeUnique<Counted>(heap, 42);
		CHECK(owned != nullptr);
		CHECK(owned->value == 42);
		CHECK(s_liveCount == 1);
		CHECK(heap.GetStatistics().liveAllocationCount == 1);
	}

	CHECK(s_liveCount == 0);
	CHECK(heap.GetStatistics().liveAllocationCount == 0);

	fang::DestroyHeap(heap);
}


TEST_CASE("アロケータを名指ししない確保は既定のヒープを通る")
{
	const fang::HeapAllocator&      defaultHeap = fang::HeapAllocator::GetInstance();
	const fang::AllocatorStatistics before      = defaultHeap.GetStatistics();

	int* value = new int(5);
	CHECK(*value == 5);
	CHECK(defaultHeap.GetStatistics().totalAllocationCount > before.totalAllocationCount);
	CHECK(defaultHeap.GetStatistics().usedBytes > before.usedBytes);

	delete value;
	CHECK(defaultHeap.GetStatistics().usedBytes == before.usedBytes);
}


TEST_CASE("標準ライブラリの隠れた確保も既定のヒープを通る")
{
	const fang::HeapAllocator&      defaultHeap = fang::HeapAllocator::GetInstance();
	const fang::AllocatorStatistics before      = defaultHeap.GetStatistics();

	{
		std::vector<int> numbers;
		numbers.reserve(1024);

		// 短い文字列は確保しないので、そうならない長さにする。
		const std::string text(512, 'a');

		CHECK(numbers.capacity() >= 1024);
		CHECK(text.size() == 512);
		CHECK(defaultHeap.GetStatistics().totalAllocationCount >= before.totalAllocationCount + 2);
	}

	CHECK(defaultHeap.GetStatistics().usedBytes == before.usedBytes);
}


TEST_CASE("nothrow の形は止めずに返す")
{
	int* value = new (std::nothrow) int(3);
	CHECK(value != nullptr);
	CHECK(*value == 3);

	delete value;
}
