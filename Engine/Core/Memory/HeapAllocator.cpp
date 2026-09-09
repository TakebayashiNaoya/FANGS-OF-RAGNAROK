/**
 * @file HeapAllocator.cpp
 * @brief 汎用ヒープの実装と、名前付きヒープの登録簿。
 */
#include "Pch.h"
#include "Core/Memory/HeapAllocator.h"
#include "Core/Log/PlatformLogSink.h"
#include "Core/Memory/New.h"
#include <cstdio>
#include <cstdlib>


namespace fang
{
	namespace
	{
		constinit detail::DefaultHeapStorage s_defaultHeapStorage{};

		/**
		 * @brief 名前付きヒープの登録簿の先頭。
		 * @details ヒープ自身が次を指す侵入型の単方向リスト。別に表を持つと、その表の確保が新しい確保を呼ぶ。
		 */
		constinit std::atomic<HeapAllocator*> s_heapListHead{ nullptr };

		/** @brief 登録簿を触る間の錠。定数初期化できるように std::mutex ではなく atomic_flag を使う。 */
		constinit std::atomic_flag s_heapListLock{};

		/**
		 * @brief std::atomic_flag のスピンロックを RAII で持つ。
		 * @details 守る範囲はどれも数命令なので、待ち行列を持つ錠より回して待つほうが速い。
		 */
		class AtomicFlagLock
		{
		public:
			FANG_NON_COPYABLE(AtomicFlagLock);
			FANG_NON_MOVABLE(AtomicFlagLock);

			explicit AtomicFlagLock(std::atomic_flag& flag)
				: m_flag(flag)
			{
				while (m_flag.test_and_set(std::memory_order_acquire))
				{
				}
			}
			~AtomicFlagLock() { m_flag.clear(std::memory_order_release); }


		private:
			std::atomic_flag& m_flag;
		};

		/**
		 * @brief 報告の 1 行を出す。
		 * @details FANG_LOG_* は裏の std::format が確保する。
		 *          リークを数えている間は錠を持っているので、そこから同じヒープを確保すると行き詰まる。
		 *          ここだけはスタックのバッファで組み立てて、直に書く。
		 */
		void WriteReportLine(const char* line)
		{
			std::fputs(line, stderr);
			WriteLogToPlatform(line);
		}
	} // namespace


	HeapAllocator& HeapAllocator::GetInstance()
	{
		return s_defaultHeapStorage.heap;
	}


	HeapAllocator& CreateHeap(const char* name)
	{
		FANG_ASSERT(name != nullptr, "ヒープの名前が nullptr");

		// 自分の new を自分で使う。確保できなければ operator new の側が止めるので nullptr 検査は要らない。
		HeapAllocator* heap = new (HeapAllocator::GetInstance()) HeapAllocator(name);

		const AtomicFlagLock lock{ s_heapListLock };
		heap->m_nextHeap = s_heapListHead.load(std::memory_order_relaxed);
		s_heapListHead.store(heap, std::memory_order_relaxed);

		return *heap;
	}


	void DestroyHeap(HeapAllocator& heap)
	{
		HeapAllocator& defaultHeap = HeapAllocator::GetInstance();
		FANG_ASSERT(&heap != &defaultHeap, "既定のヒープは壊せない");
		if (&heap == &defaultHeap)
		{
			return;
		}

		{
			const AtomicFlagLock lock{ s_heapListLock };

			HeapAllocator* previous = nullptr;
			HeapAllocator* current  = s_heapListHead.load(std::memory_order_relaxed);
			while (current != nullptr && current != &heap)
			{
				previous = current;
				current  = current->m_nextHeap;
			}

			// 見つからないのは、同じヒープを 2 度壊したか、CreateHeap で作っていないものを渡したとき。
			FANG_ASSERT(current != nullptr, "登録簿に無いヒープを壊そうとした");
			if (current == nullptr)
			{
				return;
			}

			if (previous == nullptr)
			{
				s_heapListHead.store(current->m_nextHeap, std::memory_order_relaxed);
			}
			else
			{
				previous->m_nextHeap = current->m_nextHeap;
			}
		}

		// リークの中身は追跡を入れる回で出す。今は件数だけ報せる。
		const uint64_t leakCount = heap.GetStatistics().liveAllocationCount;
		if (leakCount != 0)
		{
			char line[256]{};
			std::snprintf(
				line,
				sizeof(line),
				"[Core][Warning] ヒープ \"%s\" にリークが %llu 件ある\n",
				heap.GetName(),
				static_cast<unsigned long long>(leakCount)
			);
			WriteReportLine(line);
		}
		FANG_ASSERT(leakCount == 0, "リークを残したままヒープを壊そうとした: {}", heap.GetName());

		delete &heap;
	}


	void ForEachHeap(void (*visitor)(HeapAllocator& heap, void* userData), void* userData)
	{
		FANG_ASSERT(visitor != nullptr, "visitor が nullptr");

		const AtomicFlagLock lock{ s_heapListLock };

		HeapAllocator* heap = s_heapListHead.load(std::memory_order_relaxed);
		while (heap != nullptr)
		{
			visitor(*heap, userData);
			heap = heap->m_nextHeap;
		}
	}


	void* HeapAllocator::Allocate(size_t size, size_t alignment)
	{
		const size_t prefixSize = GetAllocationPrefixSize(alignment);

		// _aligned_malloc は 2 のべき乗の境界しか受け取らない。ヘッダの分を足して取る。
		void* block = ::_aligned_malloc(prefixSize + size, alignment);
		if (block == nullptr)
		{
			return nullptr;
		}

		m_totalAllocationCount.fetch_add(1, std::memory_order_relaxed);
		m_liveAllocationCount.fetch_add(1, std::memory_order_relaxed);
		UpdatePeakBytes(m_usedBytes.fetch_add(size, std::memory_order_relaxed) + size);

		return WriteAllocationHeader(block, *this, size, alignment);
	}


	void HeapAllocator::Deallocate(void* memory)
	{
		if (memory == nullptr)
		{
			return;
		}

		// 解放してからでは読めないので、先にヘッダを読む。
		const AllocationHeader& header = ReadAllocationHeader(memory);
		FANG_ASSERT(header.allocator == this, "確保したときと違うヒープへ返している");

		m_usedBytes.fetch_sub(header.size, std::memory_order_relaxed);
		m_liveAllocationCount.fetch_sub(1, std::memory_order_relaxed);

		::_aligned_free(static_cast<unsigned char*>(memory) - header.offsetToBlock);
	}


	AllocatorStatistics HeapAllocator::GetStatistics() const
	{
		AllocatorStatistics statistics{};
		statistics.usedBytes            = m_usedBytes.load(std::memory_order_relaxed);
		statistics.peakBytes            = m_peakBytes.load(std::memory_order_relaxed);
		statistics.liveAllocationCount  = m_liveAllocationCount.load(std::memory_order_relaxed);
		statistics.totalAllocationCount = m_totalAllocationCount.load(std::memory_order_relaxed);

		return statistics;
	}


	void HeapAllocator::UpdatePeakBytes(uint64_t usedBytes)
	{
		// 他のスレッドがもっと高い水位を書いていたら、そちらを残す。
		uint64_t peakBytes = m_peakBytes.load(std::memory_order_relaxed);
		while (peakBytes < usedBytes &&
			   !m_peakBytes.compare_exchange_weak(peakBytes, usedBytes, std::memory_order_relaxed))
		{
		}
	}
} // namespace fang
