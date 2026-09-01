/**
 * @file Allocator.cpp
 * @brief ヒープアロケータの実装。
 */
#include "Pch.h"
#include "Core/Memory/Allocator.h"
#include <cstdlib>


namespace fang
{
	void* HeapAllocator::Allocate(size_t size, size_t alignment)
	{
		// _aligned_malloc は 2 の冪の alignment しか受け取らない。
		return ::_aligned_malloc(size, alignment);
	}

	void HeapAllocator::Deallocate(void* memory)
	{
		::_aligned_free(memory);
	}

	HeapAllocator& HeapAllocator::GetInstance()
	{
		static HeapAllocator s_instance;
		return s_instance;
	}
} // namespace fang
