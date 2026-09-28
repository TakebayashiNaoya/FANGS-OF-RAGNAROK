/**
 * @file StackAllocatorTests.cpp
 * @brief スタックアロケータのテスト。
 */
#include "Core/Memory/Allocator.h"
#include "Core/Memory/HeapAllocator.h"
#include "Core/Memory/New.h"
#include "Core/Memory/StackAllocator.h"
#include <doctest.h>
#include <cstdint>
#include <string_view>


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


TEST_CASE("後に取った物から返すと先端が取る前の位置へ戻る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元1");

	{
		fang::StackAllocator stack{ "後に取った物から返す", heap, 65536 };

		const size_t alignments[] = { 16, 64, 4096, 16 };
		size_t       usedBefore[4]{};
		void*        pointers[4]{};

		for (size_t index = 0; index < 4; ++index)
		{
			usedBefore[index] = stack.GetStatistics().usedBytes;
			pointers[index]   = stack.Allocate(8, alignments[index]);
			CHECK(pointers[index] != nullptr);
		}

		for (size_t index = 4; index-- > 0;)
		{
			stack.Deallocate(pointers[index]);
			CHECK(stack.GetStatistics().usedBytes == usedBefore[index]);
		}
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("delete は取る前の位置へ戻す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元2");
	s_liveCount               = 0;

	{
		fang::StackAllocator stack{ "delete", heap, 4096 };

		Counted* object = new (stack) Counted(7);
		CHECK(object->value == 7);
		CHECK(s_liveCount == 1);

		delete object;

		CHECK(s_liveCount == 0);
		CHECK(stack.GetStatistics().usedBytes == 0);
		CHECK(stack.GetStatistics().liveAllocationCount == 0);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("境界の大きい型を delete しても詰め物ごと戻る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元3");

	{
		fang::StackAllocator stack{ "詰め物", heap, 4096 };

		CHECK(stack.Allocate(1) != nullptr); // 先端を 16 の倍数からずらす。
		const size_t usedBeforeWide = stack.GetStatistics().usedBytes;

		WideAligned* object = new (stack) WideAligned(9);
		CHECK((reinterpret_cast<uintptr_t>(object) % 64) == 0);

		delete object;
		CHECK(stack.GetStatistics().usedBytes == usedBeforeWide);

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("境界の上限でも取る前の位置へ戻る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元4");

	{
		fang::StackAllocator stack{ "上限", heap, 128 * 1024 };

		CHECK(stack.Allocate(1) != nullptr);
		const size_t usedBefore = stack.GetStatistics().usedBytes;

		void* wide = stack.Allocate(8, fang::MAXIMUM_ALLOCATION_ALIGNMENT);
		CHECK(wide != nullptr);
		CHECK((reinterpret_cast<uintptr_t>(wide) % fang::MAXIMUM_ALLOCATION_ALIGNMENT) == 0);

		stack.Deallocate(wide);
		CHECK(stack.GetStatistics().usedBytes == usedBefore);

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("印まで戻すと印より後の確保がまとめて無くなる")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元5");

	{
		fang::StackAllocator stack{ "印", heap, 4096 };

		CHECK(stack.Allocate(16) != nullptr);
		const fang::StackMarker marker = stack.GetMarker();

		CHECK(stack.Allocate(16) != nullptr);
		CHECK(stack.Allocate(32) != nullptr);
		CHECK(stack.Allocate(64) != nullptr);

		stack.FreeToMarker(marker);

		CHECK(stack.GetStatistics().usedBytes == marker.top);
		CHECK(stack.GetStatistics().liveAllocationCount == marker.liveAllocationCount);

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("印まで戻した後の確保は同じ場所を使い回す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元6");

	{
		fang::StackAllocator stack{ "使い回す", heap, 4096 };

		const fang::StackMarker marker = stack.GetMarker();
		void*                   first  = stack.Allocate(32, 16);

		stack.FreeToMarker(marker);

		void* second = stack.Allocate(32, 16);
		CHECK(second == first);

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("FreeAll は先頭の印まで戻すのと同じ")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元7");

	{
		fang::StackAllocator stack{ "FreeAll", heap, 4096 };

		const fang::StackMarker marker = stack.GetMarker();
		CHECK(stack.Allocate(16) != nullptr);
		CHECK(stack.Allocate(32) != nullptr);

		stack.FreeAll();

		CHECK(stack.GetStatistics().usedBytes == marker.top);
		CHECK(stack.GetStatistics().liveAllocationCount == marker.liveAllocationCount);
		CHECK(stack.IsEmpty());
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("頼んだ境界に載ったポインタが返る")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元8");

	{
		fang::StackAllocator stack{ "境界", heap, 65536 };

		const size_t alignments[] = { 16, 32, 64, 256, 4096 };
		for (const size_t alignment : alignments)
		{
			void* memory = stack.Allocate(100, alignment);
			CHECK(memory != nullptr);
			CHECK((reinterpret_cast<uintptr_t>(memory) % alignment) == 0);
		}

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("境界の大きい型は align_val_t 付きの new が選ばれる")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元9");

	{
		fang::StackAllocator stack{ "align_val_t", heap, 4096 };

		WideAligned* object = new (stack) WideAligned(9);
		CHECK((reinterpret_cast<uintptr_t>(object) % 64) == 0);

		delete object;
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("16 未満の境界は 16 に切り上げる")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元10");

	{
		fang::StackAllocator stack{ "16 未満", heap, 4096 };

		CHECK(stack.Allocate(1) != nullptr); // 先端を 1 バイトずらす。

		const size_t alignments[] = { 1, 2, 4, 8 };
		for (const size_t alignment : alignments)
		{
			const size_t usedBefore = stack.GetStatistics().usedBytes;

			void* memory = stack.Allocate(8, alignment);
			CHECK(memory != nullptr);
			CHECK((reinterpret_cast<uintptr_t>(memory) % 16) == 0);

			const fang::AllocationHeader& header = fang::ReadAllocationHeader(memory);
			CHECK(header.size == 8);

			stack.Deallocate(memory);
			CHECK(stack.GetStatistics().usedBytes == usedBefore);
		}

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("容量を超える確保は nullptr を返し先端は動かない")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元11");

	{
		fang::StackAllocator stack{ "容量超え", heap, 256 };

		const fang::AllocatorStatistics before = stack.GetStatistics();
		void*                           memory = stack.Allocate(1000);
		CHECK(memory == nullptr);

		const fang::AllocatorStatistics after = stack.GetStatistics();
		CHECK(after.usedBytes == before.usedBytes);
		CHECK(after.liveAllocationCount == before.liveAllocationCount);
		CHECK(after.totalAllocationCount == before.totalAllocationCount);

		void* recovered = stack.Allocate(16);
		CHECK(recovered != nullptr);

		stack.FreeAll();
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("容量ちょうどまでは取れ、その先は前置きも入らない")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元12");

	{
		const size_t         capacity = fang::GetAllocationPrefixSize(16) + 100;
		fang::StackAllocator stack{ "容量ちょうど", heap, capacity };

		void* first = stack.Allocate(100);
		CHECK(first != nullptr);
		CHECK(stack.GetStatistics().usedBytes == capacity);

		unsigned char* bytes = static_cast<unsigned char*>(first);
		for (size_t index = 0; index < 100; ++index)
		{
			bytes[index] = static_cast<unsigned char>(index & 0xFF);
		}

		const fang::AllocatorStatistics before = stack.GetStatistics();
		void*                           second = stack.Allocate(1);
		CHECK(second == nullptr);
		CHECK(stack.GetStatistics().usedBytes == before.usedBytes);

		CHECK(bytes[0] == 0);
		CHECK(bytes[99] == static_cast<unsigned char>(99 & 0xFF));

		stack.Deallocate(first);
		CHECK(stack.GetStatistics().usedBytes == 0);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("取り出し元からは 1 塊しか取らず壊すと返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元13");

	const fang::AllocatorStatistics before = heap.GetStatistics();

	{
		constexpr size_t     capacity = 4096;
		fang::StackAllocator stack{ "1 塊", heap, capacity };

		CHECK(heap.GetStatistics().liveAllocationCount == before.liveAllocationCount + 1);
		CHECK(heap.GetStatistics().usedBytes - before.usedBytes == capacity);

		CHECK(stack.Allocate(16) != nullptr);
		CHECK(stack.Allocate(32) != nullptr);
		CHECK(stack.Allocate(64) != nullptr);
		CHECK(stack.Allocate(128) != nullptr);
		CHECK(stack.Allocate(256) != nullptr);

		CHECK(heap.GetStatistics().liveAllocationCount == before.liveAllocationCount + 1);

		stack.FreeAll();
	}

	CHECK(heap.GetStatistics().liveAllocationCount == before.liveAllocationCount);

	fang::DestroyHeap(heap);
}


TEST_CASE("統計は先端の位置で数える")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元14");

	{
		fang::StackAllocator stack{ "統計", heap, 4096 };

		const fang::AllocatorStatistics before = stack.GetStatistics();
		void*                           memory = stack.Allocate(100);
		CHECK(memory != nullptr);

		const fang::AllocatorStatistics after = stack.GetStatistics();
		CHECK(after.usedBytes - before.usedBytes >= 100 + sizeof(fang::AllocationHeader));
		CHECK(after.liveAllocationCount == before.liveAllocationCount + 1);
		CHECK(after.totalAllocationCount == before.totalAllocationCount + 1);

		const uint64_t peakAfterAllocate = after.peakBytes;

		stack.Deallocate(memory);
		const fang::AllocatorStatistics returned = stack.GetStatistics();
		CHECK(returned.usedBytes - before.usedBytes == 0);
		CHECK(returned.peakBytes == peakAfterAllocate);
		CHECK(returned.liveAllocationCount == before.liveAllocationCount);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("IAllocator 越しに統計が読める")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元15");

	{
		fang::StackAllocator stack{ "IAllocator 越し", heap, 4096 };
		fang::IAllocator&    allocator = stack;

		void* memory = allocator.Allocate(64);
		CHECK(memory != nullptr);
		CHECK(allocator.GetStatistics().liveAllocationCount == 1);

		allocator.Deallocate(memory);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("nullptr の解放は何もしない")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元16");

	{
		fang::StackAllocator stack{ "nullptr", heap, 256 };
		stack.Deallocate(nullptr);
		CHECK(stack.GetStatistics().liveAllocationCount == 0);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("名前を返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元17");

	{
		fang::StackAllocator stack{ "スタックの名前", heap, 256 };
		CHECK(std::string_view(stack.GetName()) == "スタックの名前");
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("確保ヘッダの返す先はスタック自身")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元18");

	{
		fang::StackAllocator stack{ "ヘッダ", heap, 256 };

		void* memory = stack.Allocate(16);
		CHECK(memory != nullptr);

		const fang::AllocationHeader& header = fang::ReadAllocationHeader(memory);
		CHECK(header.allocator == &stack);
		CHECK(fang::IsAllocationHeaderMagic(header.magic));

		stack.Deallocate(memory);
	}

	fang::DestroyHeap(heap);
}


TEST_CASE("MakeUnique はスタックから取って抜けたら返す")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元19");
	s_liveCount               = 0;

	{
		fang::StackAllocator stack{ "MakeUnique", heap, 256 };

		{
			std::unique_ptr<Counted> owned = fang::MakeUnique<Counted>(stack, 42);
			CHECK(owned->value == 42);
			CHECK(s_liveCount == 1);
		}

		CHECK(s_liveCount == 0);
		CHECK(stack.GetStatistics().liveAllocationCount == 0);
	}

	fang::DestroyHeap(heap);
}


#if FANG_ENABLE_MEMORY_TRACKING
TEST_CASE("スタックの確保には追跡記録が付かない")
{
	fang::HeapAllocator& heap = fang::CreateHeap("スタックの取り出し元20");

	{
		fang::StackAllocator stack{ "追跡記録", heap, 256 };

		void* memory = stack.Allocate(16);
		CHECK(memory != nullptr);

		const fang::AllocationHeader& header = fang::ReadAllocationHeader(memory);
		CHECK(header.magic == fang::ALLOCATION_HEADER_MAGIC);
		CHECK(fang::FindAllocationRecord(memory) == nullptr);

		stack.Deallocate(memory);
	}

	fang::DestroyHeap(heap);
}
#endif
