/**
 * @file MemoryUsage.cpp
 * @brief メモリ使用量の取得（Windows）。
 */
#include "Pch.h"
#include "Core/Platform/MemoryUsage.h"
// clang-format off
// psapi.h は windows.h が定義する型を使うので、この 2 本だけ並べ替えさせない。
#include <windows.h>
#include <psapi.h>
// clang-format on

#pragma comment(lib, "Psapi.lib")


namespace fang
{
	MemoryUsage GetProcessMemoryUsage()
	{
		MemoryUsage usage;

		PROCESS_MEMORY_COUNTERS_EX counters{};
		counters.cb = sizeof(counters);
		if (::GetProcessMemoryInfo(
				::GetCurrentProcess(),
				reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
				sizeof(counters)
			) == 0)
		{
			return usage;
		}

		// PrivateUsage はコミット済みでこのプロセス専用の量。Xbox の AppMemoryUsage に近いのはこちら。
		usage.usedBytes = static_cast<uint64_t>(counters.PrivateUsage);

		// PC にはアプリ単位の上限が無い。予算の判定は budget::MEMORY_LIMIT_BYTES で行う。
		usage.limitBytes = 0;

		return usage;
	}
} // namespace fang
