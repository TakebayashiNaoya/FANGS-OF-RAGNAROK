/**
 * @file FrameAllocator.cpp
 * @brief フレームアロケータの実装。リニア確保、アラインメント、リセット、2 枚の入れ替え。
 */
#include "Pch.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/CoreLog.h"


namespace fang
{
	namespace
	{
		/** @brief value を alignment（2 のべき乗）の倍数へ切り上げる。 */
		template <typename T> constexpr T AlignUp(T value, size_t alignment)
		{
			const T mask = static_cast<T>(alignment) - 1;
			return (value + mask) & ~mask;
		}


		/**
		 * @brief 満杯を知らせる。
		 * @details 呼び出し側には nullptr しか届かず理由が分からないので、どこがどれだけ要求したかを残す。
		 */
		void ReportOutOfMemory(const char* name, size_t size, size_t alignment, size_t capacity)
		{
			FANG_LOG_ERROR(
				Core,
				"フレームアロケータ {} が満杯 ({} バイト / 境界 {} の要求に対し、容量は {} バイト)",
				name,
				size,
				alignment,
				capacity
			);
			FANG_ASSERT(false, "フレームアロケータが満杯。容量を増やすか、フレーム内の確保を減らすこと");
		}
	} // namespace


	FrameAllocator::~FrameAllocator()
	{
		Shutdown();
	}


	bool FrameAllocator::Initialize(IAllocator& sourceAllocator, size_t capacity, const char* name)
	{
		FANG_ASSERT(m_memory == nullptr, "フレームアロケータを二重に初期化している");
		FANG_ASSERT(name != nullptr, "フレームアロケータの名前が空");

		if (capacity == 0)
		{
			FANG_LOG_ERROR(Core, "フレームアロケータ {} の容量が 0", name);
			return false;
		}

		// 先頭を 16 境界で取っておけば、以降は 16 の倍数だけ進めるので境界を確かめ直さずに済む。
		void* memory = sourceAllocator.Allocate(capacity, DEFAULT_ALIGNMENT);
		if (memory == nullptr)
		{
			FANG_LOG_ERROR(Core, "フレームアロケータ {} の土台を確保できなかった ({} バイト)", name, capacity);
			return false;
		}

		m_sourceAllocator = &sourceAllocator;
		m_memory          = static_cast<unsigned char*>(memory);

		m_capacity = capacity;
		m_name     = name;

		m_offset.store(0, std::memory_order_relaxed);

#if FANG_ENABLE_PROFILER
		m_allocationCount.store(0, std::memory_order_relaxed);
		m_peakUsedBytes = 0;
#endif

		return true;
	}


	void FrameAllocator::Shutdown()
	{
		if (m_memory == nullptr)
		{
			return;
		}

		m_sourceAllocator->Deallocate(m_memory);

		m_sourceAllocator = nullptr;
		m_memory          = nullptr;

		m_capacity = 0;

		// 名前は呼ぶ側が持っている文字列なので、土台を返したら参照をやめる。
		m_name = "Frame";

		m_offset.store(0, std::memory_order_relaxed);
	}


	void FrameAllocator::Reset()
	{
#if FANG_ENABLE_ASSERT
		// オフセットを戻し終えるまでは奇数にしておく。その間に走った Allocate は配った先を踏み倒される。
		m_resetGeneration.fetch_add(1, std::memory_order_relaxed);
#endif

#if FANG_ENABLE_PROFILER
		// リニアに配るので、リセット直前のオフセットがそのフレームの最大使用量そのもの。
		const size_t usedBytes = m_offset.load(std::memory_order_relaxed);
		if (usedBytes > m_peakUsedBytes)
		{
			m_peakUsedBytes = usedBytes;
		}

		m_allocationCount.store(0, std::memory_order_relaxed);
#endif

		m_offset.store(0, std::memory_order_relaxed);

#if FANG_ENABLE_ASSERT
		m_resetGeneration.fetch_add(1, std::memory_order_relaxed);
#endif
	}


	void* FrameAllocator::Allocate(size_t size, size_t alignment)
	{
		FANG_ASSERT(m_memory != nullptr, "初期化していないフレームアロケータから確保した");
		FANG_ASSERT(
			alignment != 0 && (alignment & (alignment - 1)) == 0,
			"アラインメント {} が 2 のべき乗でない",
			alignment
		);

#if FANG_ENABLE_ASSERT
		FANG_ASSERT(
			(m_resetGeneration.load(std::memory_order_relaxed) & 1u) == 0,
			"リセット中に Allocate が走った。ジョブを待ち切ってから BeginFrame を呼ぶこと"
		);
#endif

		// 1 件で容量を超える要求は、切り上げる前に弾く。➡この後の足し算が桁あふれしない。
		if (size > m_capacity)
		{
			ReportOutOfMemory(m_name, size, alignment, m_capacity);
			return nullptr;
		}

		// 先頭を常に 16 境界へ置くため、確保幅そのものを 16 の倍数へ切り上げる。
		size_t requestedSize = AlignUp(size, DEFAULT_ALIGNMENT);
		if (alignment > DEFAULT_ALIGNMENT)
		{
			// 16 境界から目的の境界まで進める余地を足す。余った端はこのフレームの間だけ捨てる。
			requestedSize += alignment - DEFAULT_ALIGNMENT;
		}

		// ロックもリストも持たず、進める位置を取り合うだけ。➡ジョブから同時に呼んでも待たない。
		const size_t offset = m_offset.fetch_add(requestedSize, std::memory_order_relaxed);

		// 引き算で比べているのは、満杯を踏み続けた後の offset + requestedSize が桁あふれしうるため。
		if (requestedSize > m_capacity || offset > m_capacity - requestedSize)
		{
			ReportOutOfMemory(m_name, size, alignment, m_capacity);
			return nullptr;
		}

#if FANG_ENABLE_PROFILER
		m_allocationCount.fetch_add(1, std::memory_order_relaxed);
#endif

		const uintptr_t address = reinterpret_cast<uintptr_t>(m_memory + offset);
		return reinterpret_cast<void*>(AlignUp(address, alignment));
	}


	void FrameAllocator::Deallocate(void* memory)
	{
		// フレーム境界の Reset でまとめて空くので、1 件ずつ返すことはしない。
		FANG_UNUSED(memory);
	}


	bool FrameMemory::Initialize(const FrameMemoryDesc& desc)
	{
		// ログで 2 枚を見分けるための名前。リテラルなので寿命を気にしなくてよい。
		constexpr const char* bufferNames[BUFFER_COUNT] = { "Frame 0", "Frame 1" };

		IAllocator& sourceAllocator = HeapAllocator::GetInstance();
		for (size_t i = 0; i < BUFFER_COUNT; ++i)
		{
			if (!m_buffers[i].Initialize(sourceAllocator, desc.capacityPerBuffer, bufferNames[i]))
			{
				Shutdown();
				return false;
			}
		}

		m_currentIndex = 0;

		return true;
	}


	void FrameMemory::Shutdown()
	{
		for (FrameAllocator& buffer : m_buffers)
		{
			buffer.Shutdown();
		}

		m_currentIndex = 0;
	}


	void FrameMemory::BeginFrame()
	{
		// 入れ替えてから、今のフレームになった側だけを空ける。前のフレームの 1 枚はそのまま残す。
		m_currentIndex = (m_currentIndex + 1) % BUFFER_COUNT;
		m_buffers[m_currentIndex].Reset();
	}
} // namespace fang
