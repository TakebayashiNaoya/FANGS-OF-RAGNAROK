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

#if FANG_ENABLE_MEMORY_TRACKING
		/**
		 * @brief 追跡記録から確保ヘッダを引く。
		 * @details 記録の直後にヘッダが並ぶ配置なので、1 つ足せば届く。
		 */
		[[nodiscard]] const AllocationHeader* GetHeaderFromRecord(const AllocationRecord* record)
		{
			return reinterpret_cast<const AllocationHeader*>(record + 1);
		}

		/**
		 * @brief パスの最後の要素だけを返す。
		 * @details std::source_location::file_name() は絶対パスになるので、報告にはファイル名だけを出す。
		 */
		[[nodiscard]] const char* FindFileNameOnly(const char* filePath)
		{
			const char* fileName = filePath;
			for (const char* cursor = filePath; *cursor != '\0'; ++cursor)
			{
				if (*cursor == '\\' || *cursor == '/')
				{
					fileName = cursor + 1;
				}
			}

			return fileName;
		}
#endif
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

		const uint64_t leakCount = heap.ReportLeaks();
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


	uint64_t ReportAllHeapLeaks()
	{
		uint64_t leakCount = HeapAllocator::GetInstance().ReportLeaks();

		// ここに残っているのは DestroyHeap を呼ばなかったヒープ。壊し忘れ自体もリークなので報せる。
		ForEachHeap(
			[](HeapAllocator& heap, void* userData) {
				char line[256]{};
				std::snprintf(line, sizeof(line), "[Core][Warning] ヒープ \"%s\" が壊されていない\n", heap.GetName());
				WriteReportLine(line);

				*static_cast<uint64_t*>(userData) += heap.ReportLeaks();
			},
			&leakCount
		);

		return leakCount;
	}


	void* HeapAllocator::Allocate(size_t size, size_t alignment)
	{
		const size_t prefixSize = GetAllocationPrefixSize(alignment);

		// _aligned_malloc は 2 のべき乗の境界しか受け取らない。前置きの分を足して取る。
		void* block = ::_aligned_malloc(prefixSize + size, alignment);
		if (block == nullptr)
		{
			return nullptr;
		}

		const uint64_t previousTotalCount = m_totalAllocationCount.fetch_add(1, std::memory_order_relaxed);
		m_liveAllocationCount.fetch_add(1, std::memory_order_relaxed);
		UpdatePeakBytes(m_usedBytes.fetch_add(size, std::memory_order_relaxed) + size);

#if FANG_ENABLE_MEMORY_TRACKING
		void* userPointer = WriteAllocationHeader(block, *this, size, alignment, true);

		// 呼び出し元はここでは分からない。知っている層が後から SetAllocationSite で書く。
		AllocationRecord* record = FindAllocationRecord(userPointer);
		record->fileName         = nullptr;
		record->returnAddress    = nullptr;
		record->line             = 0;
		record->serialNumber     = static_cast<uint32_t>(previousTotalCount + 1);

		LinkAllocationRecord(record);

		return userPointer;
#else
		FANG_UNUSED(previousTotalCount);

		return WriteAllocationHeader(block, *this, size, alignment, false);
#endif
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

#if FANG_ENABLE_MEMORY_TRACKING
		AllocationRecord* record = FindAllocationRecord(memory);
		if (record != nullptr)
		{
			UnlinkAllocationRecord(record);
		}
#endif

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


	uint64_t HeapAllocator::ReportLeaks() const
	{
#if FANG_ENABLE_MEMORY_TRACKING
		const AtomicFlagLock lock{ m_liveListLock };

		uint64_t leakCount = 0;
		uint64_t leakBytes = 0;
		for (const AllocationRecord* record = m_liveListHead; record != nullptr; record = record->next)
		{
			++leakCount;
			leakBytes += GetHeaderFromRecord(record)->size;
		}

		if (leakCount == 0)
		{
			return 0;
		}

		char line[512]{};
		std::snprintf(
			line,
			sizeof(line),
			"[Core][Warning] ヒープ \"%s\" にリークが %llu 件ある。合計 %llu バイト\n",
			m_name,
			static_cast<unsigned long long>(leakCount),
			static_cast<unsigned long long>(leakBytes)
		);
		WriteReportLine(line);

		for (const AllocationRecord* record = m_liveListHead; record != nullptr; record = record->next)
		{
			const uint32_t size = GetHeaderFromRecord(record)->size;
			if (record->fileName != nullptr)
			{
				std::snprintf(
					line,
					sizeof(line),
					"[Core][Warning]   #%u %u バイト %s:%u\n",
					record->serialNumber,
					size,
					FindFileNameOnly(record->fileName),
					record->line
				);
			}
			else
			{
				const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(record->returnAddress);
				std::snprintf(
					line,
					sizeof(line),
					"[Core][Warning]   #%u %u バイト (呼び出し元不明。戻り番地 0x%016llX)\n",
					record->serialNumber,
					size,
					static_cast<unsigned long long>(returnAddress)
				);
			}
			WriteReportLine(line);
		}

		return leakCount;
#else
		// 追跡が無い構成では件数と量しか分からない。呼び出し元は Debug で取る。
		const uint64_t leakCount = m_liveAllocationCount.load(std::memory_order_relaxed);
		if (leakCount != 0)
		{
			char line[256]{};
			std::snprintf(
				line,
				sizeof(line),
				"[Core][Warning] ヒープ \"%s\" にリークが %llu 件ある。合計 %llu バイト（呼び出し元は Debug "
				"でだけ出る）\n",
				m_name,
				static_cast<unsigned long long>(leakCount),
				static_cast<unsigned long long>(m_usedBytes.load(std::memory_order_relaxed))
			);
			WriteReportLine(line);
		}

		return leakCount;
#endif
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


#if FANG_ENABLE_MEMORY_TRACKING

	void HeapAllocator::LinkAllocationRecord(AllocationRecord* record)
	{
		const AtomicFlagLock lock{ m_liveListLock };

		record->previous = nullptr;
		record->next     = m_liveListHead;
		if (m_liveListHead != nullptr)
		{
			m_liveListHead->previous = record;
		}

		m_liveListHead = record;
	}


	void HeapAllocator::UnlinkAllocationRecord(AllocationRecord* record)
	{
		const AtomicFlagLock lock{ m_liveListLock };

		if (record->previous != nullptr)
		{
			record->previous->next = record->next;
		}
		else
		{
			m_liveListHead = record->next;
		}

		if (record->next != nullptr)
		{
			record->next->previous = record->previous;
		}
	}

#endif
} // namespace fang
