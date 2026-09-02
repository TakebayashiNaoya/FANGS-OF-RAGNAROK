/**
 * @file Thread.cpp
 * @brief スレッドまわりの実装（Windows）。物理コア数の取得とスレッド名付け。
 */
#include "Pch.h"
#include "Core/Platform/Thread.h"
#include "Core/Memory/Allocator.h"
#include <windows.h>
#include <thread>


namespace fang
{
	namespace
	{
		constexpr int MAX_THREAD_NAME_LENGTH = 32;

		/** @brief 物理コアを数えられなかったときの代替。論理プロセッサ数で妥協する。 */
		uint32_t GetFallbackCoreCount()
		{
			const uint32_t logicalCount = std::thread::hardware_concurrency();
			return logicalCount > 0 ? logicalCount : 1;
		}
	} // namespace


	uint32_t GetUsableCoreCount()
	{
		// hardware_concurrency() は論理プロセッサ数なので、ハイパースレッディングがあると
		// 物理コアの倍が返る。ワーカーを倍作っても取り合いになるだけなので使わない。
		DWORD bufferSize = 0;
		if (::GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufferSize) != FALSE ||
			::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bufferSize == 0)
		{
			return GetFallbackCoreCount();
		}

		IAllocator& allocator = HeapAllocator::GetInstance();
		void*       buffer    = allocator.Allocate(bufferSize, alignof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
		if (buffer == nullptr)
		{
			return GetFallbackCoreCount();
		}

		uint32_t coreCount = 0;
		if (::GetLogicalProcessorInformationEx(
				RelationProcessorCore,
				static_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer),
				&bufferSize
			) != FALSE)
		{
			// 返ってくるのは可変長レコードの並びなので、Size の分だけ進めて数える。
			for (DWORD offset = 0; offset < bufferSize;)
			{
				const auto* information = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
					static_cast<const unsigned char*>(buffer) + offset
				);
				if (information->Size == 0)
				{
					break;
				}

				++coreCount;
				offset += information->Size;
			}
		}

		allocator.Deallocate(buffer);

		return coreCount > 0 ? coreCount : GetFallbackCoreCount();
	}


	void SetCurrentThreadName(const char* name)
	{
		if (name == nullptr)
		{
			return;
		}

		wchar_t wideName[MAX_THREAD_NAME_LENGTH]{};
		if (::MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName, MAX_THREAD_NAME_LENGTH - 1) == 0)
		{
			return;
		}

		// 名前が付かなくてもデバッグの都合が悪くなるだけなので、失敗は無視する。
		::SetThreadDescription(::GetCurrentThread(), wideName);
	}
} // namespace fang
