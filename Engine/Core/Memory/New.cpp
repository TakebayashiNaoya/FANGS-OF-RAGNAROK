/**
 * @file New.cpp
 * @brief アロケータを名指しする new と、グローバルの new / delete の置き換え。
 * @details 置き換える理由は、隠れた確保を 1 本の経路へ集めるため。
 *          標準ライブラリや外部ライブラリが裏で取ったぶんも既定のヒープを通るので、
 *          「ロードが終わった後のフレームの中でヒープ確保 0」を累計の差分で確かめられる。
 */
#include "Pch.h"
#include "Core/Memory/New.h"
#include "Core/Log/PlatformLogSink.h"
#include "Core/Memory/HeapAllocator.h"
#include <cstdio>
#include <cstdlib>

/**
 * @brief この翻訳単位をリンクへ引きずり出すための錨。
 * @details 静的ライブラリの中の定義は、誰も参照していないと取り込まれない。
 *          今の構成では exe が new を参照するのでこの翻訳単位が選ばれるが、その前提が外れると CRT のものが黙って使われる。
 *          そうなると統計も「実行中の確保 0」の検査も静かに嘘になり、気付く手立てが無い。
 *          exe 側が Build\Program.props の /INCLUDE でこの名前を必ず要求して、選ばれ方を運任せにしない。
 */
extern "C" int FangGlobalNewAnchor = 0;


namespace
{
	/**
	 * @brief 確保できなかったことを報せて止める。
	 * @details std::format は内部で確保するので、確保に失敗した場から呼ぶと同じ失敗をもう 1 度踏む。
	 *          ここだけは組み立てにバッファを使わない。
	 */
	[[noreturn]] void ReportOutOfMemory(const fang::IAllocator& allocator, size_t size, size_t alignment)
	{
		char line[256]{};
		std::snprintf(
			line,
			sizeof(line),
			"[Core][Fatal] メモリを確保できない。ヒープ=%s 要求=%zu 境界=%zu\n",
			allocator.GetName(),
			size,
			alignment
		);

		std::fputs(line, stderr);
		fang::WriteLogToPlatform(line);
		std::abort();
	}


	/** @brief 確保する。できなければ止める。例外を切っているので nullptr を返して先へ進ませない。 */
	[[nodiscard]] void* AllocateOrStop(fang::IAllocator& allocator, size_t size, size_t alignment)
	{
		void* memory = allocator.Allocate(size, alignment);
		if (memory == nullptr)
		{
			ReportOutOfMemory(allocator, size, alignment);
		}

		return memory;
	}


	/** @brief 既定のヒープから確保する。できなければ止める。 */
	[[nodiscard]] void* AllocateFromDefaultHeapOrStop(size_t size, size_t alignment)
	{
		return AllocateOrStop(fang::HeapAllocator::GetInstance(), size, alignment);
	}
} // namespace


void* operator new(size_t size, fang::IAllocator& allocator, std::source_location location)
{
	// 呼び出し元は追跡を入れる回で使う。今は捨てる。
	FANG_UNUSED(location);

	return AllocateOrStop(allocator, size, fang::IAllocator::DEFAULT_ALIGNMENT);
}


void* operator new(size_t size, std::align_val_t alignment, fang::IAllocator& allocator, std::source_location location)
{
	FANG_UNUSED(location);

	return AllocateOrStop(allocator, size, static_cast<size_t>(alignment));
}


void* operator new[](size_t size, fang::IAllocator& allocator, std::source_location location)
{
	return ::operator new(size, allocator, location);
}


void* operator new[](
	size_t               size,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location
)
{
	return ::operator new(size, alignment, allocator, location);
}


void operator delete(void* memory, fang::IAllocator& allocator, std::source_location location) noexcept
{
	FANG_UNUSED(allocator);
	FANG_UNUSED(location);

	fang::ReturnToAllocator(memory);
}


void operator delete(
	void*                memory,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location
) noexcept
{
	FANG_UNUSED(alignment);
	FANG_UNUSED(allocator);
	FANG_UNUSED(location);

	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory, fang::IAllocator& allocator, std::source_location location) noexcept
{
	::operator delete(memory, allocator, location);
}


void operator delete[](
	void*                memory,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location
) noexcept
{
	::operator delete(memory, alignment, allocator, location);
}


// ここから下はグローバルの置き換え。アロケータを名指ししない確保は全部ここを通り、既定のヒープへ落ちる。

void* operator new(size_t size)
{
	return AllocateFromDefaultHeapOrStop(size, fang::IAllocator::DEFAULT_ALIGNMENT);
}


void* operator new[](size_t size)
{
	return ::operator new(size);
}


void* operator new(size_t size, std::align_val_t alignment)
{
	return AllocateFromDefaultHeapOrStop(size, static_cast<size_t>(alignment));
}


void* operator new[](size_t size, std::align_val_t alignment)
{
	return ::operator new(size, alignment);
}


void* operator new(size_t size, const std::nothrow_t&) noexcept
{
	// nothrow の形は標準どおり nullptr を返す。止めるのは通常の形だけ。
	return fang::HeapAllocator::GetInstance().Allocate(size, fang::IAllocator::DEFAULT_ALIGNMENT);
}


void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}


void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	return fang::HeapAllocator::GetInstance().Allocate(size, static_cast<size_t>(alignment));
}


void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, alignment, tag);
}


void operator delete(void* memory) noexcept
{
	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory) noexcept
{
	fang::ReturnToAllocator(memory);
}


void operator delete(void* memory, size_t size) noexcept
{
	// 大きさはヘッダが持っているので使わない。
	FANG_UNUSED(size);

	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory, size_t size) noexcept
{
	FANG_UNUSED(size);

	fang::ReturnToAllocator(memory);
}


void operator delete(void* memory, std::align_val_t alignment) noexcept
{
	FANG_UNUSED(alignment);

	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory, std::align_val_t alignment) noexcept
{
	FANG_UNUSED(alignment);

	fang::ReturnToAllocator(memory);
}


void operator delete(void* memory, size_t size, std::align_val_t alignment) noexcept
{
	FANG_UNUSED(size);
	FANG_UNUSED(alignment);

	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory, size_t size, std::align_val_t alignment) noexcept
{
	FANG_UNUSED(size);
	FANG_UNUSED(alignment);

	fang::ReturnToAllocator(memory);
}


void operator delete(void* memory, const std::nothrow_t&) noexcept
{
	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
	fang::ReturnToAllocator(memory);
}


void operator delete(void* memory, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	FANG_UNUSED(alignment);

	fang::ReturnToAllocator(memory);
}


void operator delete[](void* memory, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	FANG_UNUSED(alignment);

	fang::ReturnToAllocator(memory);
}
