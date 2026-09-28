/**
 * @file StackAllocator.cpp
 * @brief 先端を進めるだけで確保し、後に取った物から返すアロケータの実装。
 */
#include "Pch.h"
#include "Core/Memory/StackAllocator.h"
#include "Core/CoreLog.h"


namespace
{
	/**
	 * @brief value を alignment の倍数へ切り上げる。
	 * @details alignment は 2 のべき乗であること。
	 */
	[[nodiscard]] uintptr_t AlignUp(uintptr_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
	}
} // namespace


namespace fang
{
	static_assert(MAXIMUM_ALLOCATION_ALIGNMENT * 2 - 1 <= UINT16_MAX, "詰め物と前置きの和が offsetToBlock に収まる");


	StackAllocator::StackAllocator(const char* name, IAllocator& source, size_t capacity)
		: m_name(name)
		, m_source(source)
		, m_capacity(capacity)
		, m_ownerThread(std::this_thread::get_id())
	{
		FANG_ASSERT(name != nullptr, "スタックの名前が nullptr");
		FANG_ASSERT(capacity != 0, "スタック \"{}\" の容量が 0", m_name);
		FANG_ASSERT(capacity <= MAXIMUM_ALLOCATION_SIZE, "スタック \"{}\" の容量が大きすぎる: {}", m_name, capacity);

		m_region = static_cast<unsigned char*>(m_source.Allocate(m_capacity, DEFAULT_ALIGNMENT));
		if (m_region == nullptr)
		{
			// 領域の無いスタックは以後の確保が全部失敗するだけなので、ここで止める。
			FANG_FATAL("スタック \"{}\" の領域を \"{}\" から取れない。容量={}", m_name, m_source.GetName(), m_capacity);
		}
	}


	StackAllocator::~StackAllocator()
	{
		AssertOwnerThread();
		if (m_liveAllocationCount != 0)
		{
			// 領域を返した後で古いポインタが使われると、原因から離れた場所で壊れる。
			// Release でも止める。
			FANG_FATAL("スタック \"{}\" に生きている確保が {} 件残ったまま壊そうとした", m_name, m_liveAllocationCount);
		}

		m_source.Deallocate(m_region);
	}


	void* StackAllocator::Allocate(size_t size, size_t alignment)
	{
		AssertOwnerThread();

		// ① 頼み方を検査し、配置に使う境界を決める。
		//    16 未満は 16 に切り上がる。
		const size_t effectiveAlignment = ValidateAllocationRequest(*this, size, alignment);

		// ② 利用者ポインタの位置を決める。
		//    前置きは境界の倍数なので、利用者ポインタが載ればブロック先頭も載る。
		const size_t    prefixSize  = GetAllocationPrefixSize(effectiveAlignment);
		const uintptr_t regionBase  = reinterpret_cast<uintptr_t>(m_region);
		const uintptr_t userAddress = AlignUp(regionBase + m_top + prefixSize, effectiveAlignment);
		const size_t    userOffset  = static_cast<size_t>(userAddress - regionBase);

		// ③ 容量を超えるなら先端を動かさずに返す。
		if (userOffset > m_capacity || size > m_capacity - userOffset)
		{
			FANG_LOG_ERROR(
				Core,
				"スタック \"{}\" の容量を超えた。要求={} 境界={} 使用量={} 容量={}",
				m_name,
				size,
				effectiveAlignment,
				m_top,
				m_capacity
			);
			return nullptr;
		}

		// ④ 確保ヘッダを書く。
		//    追跡記録は付けない(hasRecord = false)。
		unsigned char* block       = reinterpret_cast<unsigned char*>(userAddress - prefixSize);
		void*          userPointer = WriteAllocationHeader(block, *this, size, effectiveAlignment, false);

		// ⑤ ブロック先頭までの距離を「取る前の先端まで」に書き換える。
		//    詰め物の大きさは境界と直前の先端で変わり、返すときには分からない。
		//    ここに書いておけば、返すときに引き算 1 回で取る前の位置へ戻せる。
		AllocationHeader* header =
			reinterpret_cast<AllocationHeader*>(static_cast<unsigned char*>(userPointer) - sizeof(AllocationHeader));
		header->offsetToBlock = static_cast<uint16_t>(userOffset - m_top);

		// ⑥ 先端と統計を進める。
		m_top     = userOffset + size;
		m_peakTop = m_top > m_peakTop ? m_top : m_peakTop;
		++m_liveAllocationCount;
		++m_totalAllocationCount;

		return userPointer;
	}


	void StackAllocator::Deallocate(void* memory)
	{
		AssertOwnerThread();

		if (memory == nullptr)
		{
			return;
		}

		const AllocationHeader& header = ReadAllocationHeader(memory);
		FANG_ASSERT(header.allocator == this, "確保したときと違うスタックへ返している");

		// ① 先端にある確保だけ返せる。
		//    この 1 件の終わりが先端と一致するかを見る。
		unsigned char* bytes      = static_cast<unsigned char*>(memory);
		const size_t   userOffset = static_cast<size_t>(bytes - m_region);
		const size_t   endOffset  = userOffset + header.size;
		if (endOffset != m_top)
		{
			// 進めると先端がまだ使っている確保より手前へ戻り、次の確保がその上に置かれて静かに壊れる。
			FANG_FATAL("スタック \"{}\" へ順番を破って返した。返した確保の終わり={} 先端={}", m_name, endOffset, m_top);
		}

		// ② 取る前の先端へ戻す。
		//    offsetToBlock は詰め物を含めた距離にしてある。
		m_top = userOffset - header.offsetToBlock;
		--m_liveAllocationCount;
	}


	AllocatorStatistics StackAllocator::GetStatistics() const
	{
		AssertOwnerThread();

		AllocatorStatistics statistics{};
		statistics.usedBytes = m_top; // 先端の位置なので、確保ヘッダと詰め物を含み、容量とそのまま比べられる。
		statistics.peakBytes = m_peakTop;
		statistics.liveAllocationCount  = m_liveAllocationCount;
		statistics.totalAllocationCount = m_totalAllocationCount;

		return statistics;
	}


	StackMarker StackAllocator::GetMarker() const
	{
		AssertOwnerThread();

		return StackMarker{ .top = m_top, .liveAllocationCount = m_liveAllocationCount };
	}


	void StackAllocator::FreeToMarker(StackMarker marker)
	{
		AssertOwnerThread();
		FANG_ASSERT(
			marker.top <= m_top,
			"スタック \"{}\" の印が先端より後ろにある。印={} 先端={}",
			m_name,
			marker.top,
			m_top
		);
		FANG_ASSERT(
			marker.liveAllocationCount <= m_liveAllocationCount,
			"スタック \"{}\" の印の件数が今より多い。印={} 今={}",
			m_name,
			marker.liveAllocationCount,
			m_liveAllocationCount
		);

		m_top                 = marker.top;
		m_liveAllocationCount = marker.liveAllocationCount;
	}


	void StackAllocator::FreeAll()
	{
		AssertOwnerThread();

		m_top                 = 0;
		m_liveAllocationCount = 0;
	}


	bool StackAllocator::IsEmpty() const
	{
		AssertOwnerThread();

		return m_liveAllocationCount == 0;
	}


	void StackAllocator::AssertOwnerThread() const
	{
		FANG_ASSERT(
			std::this_thread::get_id() == m_ownerThread,
			"スタック \"{}\" を作ったスレッド以外から呼んだ",
			m_name
		);
	}
} // namespace fang
