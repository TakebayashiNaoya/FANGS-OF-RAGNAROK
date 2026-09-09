/**
 * @file Allocator.cpp
 * @brief 確保ヘッダの読み書きの実装。
 */
#include "Pch.h"
#include "Core/Memory/Allocator.h"


namespace fang
{
	void* WriteAllocationHeader(void* block, IAllocator& allocator, size_t size, size_t alignment)
	{
		FANG_ASSERT(block != nullptr, "ブロックが nullptr");
		FANG_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "境界が 2 のべき乗でない: {}", alignment);
		FANG_ASSERT(alignment <= MAXIMUM_ALLOCATION_ALIGNMENT, "境界が大きすぎる: {}", alignment);
		FANG_ASSERT(size <= MAXIMUM_ALLOCATION_SIZE, "1 件が大きすぎる: {}", size);

		const size_t   prefixSize  = GetAllocationPrefixSize(alignment);
		unsigned char* userPointer = static_cast<unsigned char*>(block) + prefixSize;

		// ヘッダは利用者ポインタの直前に置く。前置きの残りは詰め物になる。
		AllocationHeader* header = reinterpret_cast<AllocationHeader*>(userPointer - sizeof(AllocationHeader));
		header->allocator        = &allocator;
		header->size             = static_cast<uint32_t>(size);
		header->offsetToBlock    = static_cast<uint16_t>(prefixSize);
		header->magic            = ALLOCATION_HEADER_MAGIC;

		return userPointer;
	}


	const AllocationHeader& ReadAllocationHeader(const void* userPointer)
	{
		const unsigned char*    bytes  = static_cast<const unsigned char*>(userPointer);
		const AllocationHeader* header = reinterpret_cast<const AllocationHeader*>(bytes - sizeof(AllocationHeader));

		if (header->magic != ALLOCATION_HEADER_MAGIC)
		{
			// 見逃すと別のヒープを壊しに行くので、Release でも止める。
			FANG_FATAL("確保ヘッダの無いメモリを解放しようとした");
		}

		return *header;
	}


	void ReturnToAllocator(void* userPointer)
	{
		if (userPointer == nullptr)
		{
			return;
		}

		const AllocationHeader& header = ReadAllocationHeader(userPointer);
		header.allocator->Deallocate(userPointer);
	}
} // namespace fang
