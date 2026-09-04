/**
 * @file MemoryUsage.cpp
 * @brief メモリ使用量の取得（Xbox / UWP）。
 */
#include "Pch.h"
#include "Core/Platform/MemoryUsage.h"

// C++/WinRT(例外前提)を使ってよいのはこの Xbox ディレクトリの TU だけ。例外は外に出さない。
#include <winrt/Windows.System.h>


namespace fang
{
	MemoryUsage GetProcessMemoryUsage()
	{
		MemoryUsage usage;

		try
		{
			// Game 分類なら割り当てられた 5GB 前後が上限として返る。App 分類だと 1GB 前後になるので、
			// 配置の分類を間違えたことにここでも気付ける。
			usage.usedBytes  = winrt::Windows::System::MemoryManager::AppMemoryUsage();
			usage.limitBytes = winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
		}
		catch (...)
		{
			// 測れなくても動作は続ける。呼び出し側は usedBytes が 0 なら測れなかったと見る。
			return MemoryUsage();
		}

		return usage;
	}
} // namespace fang
