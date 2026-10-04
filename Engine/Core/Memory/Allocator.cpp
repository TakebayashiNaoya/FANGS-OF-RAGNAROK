/**
 * @file Allocator.cpp
 * @brief 確保ヘッダと追跡記録の読み書きの実装。
 */
#include "Pch.h"
#include "Core/Memory/Allocator.h"


namespace fang
{
	size_t ValidateAllocationRequest(const IAllocator& allocator, size_t size, size_t alignment)
	{
		// どれも呼ぶ側の誤りなので Release でも止める。
		if (alignment == 0 || (alignment & (alignment - 1)) != 0)
		{
			FANG_FATAL("アロケータ \"{}\" へ 2 のべき乗でない境界を頼んだ: {}", allocator.GetName(), alignment);
		}

		if (alignment > MAXIMUM_ALLOCATION_ALIGNMENT)
		{
			FANG_FATAL(
				"アロケータ \"{}\" へ上限を超える境界を頼んだ: {} 上限={}",
				allocator.GetName(),
				alignment,
				MAXIMUM_ALLOCATION_ALIGNMENT
			);
		}

		if (size > MAXIMUM_ALLOCATION_SIZE)
		{
			FANG_FATAL(
				"アロケータ \"{}\" へ上限を超える大きさを頼んだ: {} 上限={}",
				allocator.GetName(),
				size,
				MAXIMUM_ALLOCATION_SIZE
			);
		}

		return alignment < IAllocator::DEFAULT_ALIGNMENT ? IAllocator::DEFAULT_ALIGNMENT : alignment;
	}


	void* WriteAllocationHeader(void* block, IAllocator& allocator, size_t size, size_t alignment, bool hasRecord)
	{
		FANG_ASSERT(block != nullptr, "ブロックが nullptr");
		FANG_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "境界が 2 のべき乗でない: {}", alignment);
		FANG_ASSERT(alignment <= MAXIMUM_ALLOCATION_ALIGNMENT, "境界が大きすぎる: {}", alignment);
		FANG_ASSERT(size <= MAXIMUM_ALLOCATION_SIZE, "1 件が大きすぎる: {}", size);
		FANG_ASSERT(
			alignment >= IAllocator::DEFAULT_ALIGNMENT,
			"境界が 16 未満: {}。ValidateAllocationRequest を通していない",
			alignment
		);

		const size_t   prefixSize  = GetAllocationPrefixSize(alignment);
		unsigned char* userPointer = static_cast<unsigned char*>(block) + prefixSize;

		// ヘッダは利用者ポインタの直前に置く。前置きの残りは詰め物と、追跡を入れた構成では追跡記録になる。
		AllocationHeader* header = reinterpret_cast<AllocationHeader*>(userPointer - sizeof(AllocationHeader));
		header->allocator        = &allocator;
		header->size             = static_cast<uint32_t>(size);
		header->offsetToBlock    = static_cast<uint16_t>(prefixSize);
		header->magic            = hasRecord ? ALLOCATION_HEADER_MAGIC_TRACKED : ALLOCATION_HEADER_MAGIC;

		return userPointer;
	}


	const AllocationHeader& ReadAllocationHeader(const void* userPointer)
	{
		const unsigned char*    bytes  = static_cast<const unsigned char*>(userPointer);
		const AllocationHeader* header = reinterpret_cast<const AllocationHeader*>(bytes - sizeof(AllocationHeader));

		if (!IsAllocationHeaderMagic(header->magic))
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


#if FANG_ENABLE_MEMORY_TRACKING

	AllocationRecord* FindAllocationRecord(void* userPointer)
	{
		if (userPointer == nullptr)
		{
			return nullptr;
		}

		unsigned char*          bytes  = static_cast<unsigned char*>(userPointer);
		const AllocationHeader* header = reinterpret_cast<const AllocationHeader*>(bytes - sizeof(AllocationHeader));
		if (header->magic != ALLOCATION_HEADER_MAGIC_TRACKED)
		{
			return nullptr;
		}

		// 記録の位置は境界によらず一定にしてある。
		// 境界を変えても前置きの中で伸び縮みするのは詰め物の側だけなので、この引き算 1 回で届く。
		// 境界は ValidateAllocationRequest が 16 以上に切り上げるので、この位置は必ず 8 の倍数になり、記録の境界要求も満たす。
		return reinterpret_cast<AllocationRecord*>(bytes - sizeof(AllocationHeader) - sizeof(AllocationRecord));
	}


	void SetAllocationSite(void* userPointer, const char* fileName, uint32_t line)
	{
		AllocationRecord* record = FindAllocationRecord(userPointer);
		if (record == nullptr)
		{
			return;
		}

		record->fileName = fileName;
		record->line     = line;
	}


	void SetAllocationSite(void* userPointer, const void* returnAddress)
	{
		AllocationRecord* record = FindAllocationRecord(userPointer);
		if (record == nullptr)
		{
			return;
		}

		record->returnAddress = returnAddress;
	}

#endif
} // namespace fang
