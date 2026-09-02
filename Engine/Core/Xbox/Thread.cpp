/**
 * @file Thread.cpp
 * @brief スレッドまわりの実装（Xbox / UWP）。物理コア数の取得とスレッド名付け。
 */
#include "Pch.h"
#include "Core/Platform/Thread.h"
#include <windows.h>


namespace fang
{
	namespace
	{
		constexpr int MAX_THREAD_NAME_LENGTH = 32;

		/** @brief Xbox One の 8 コアのうち、アプリが使えるのは占有 4 + 共有 2 の 6 個。 */
		constexpr uint32_t XBOX_AVAILABLE_CORE_COUNT = 6;
	} // namespace


	uint32_t GetPhysicalCoreCount()
	{
		// Jaguar 系のコアは SMT を持たないので、見えている論理プロセッサ数がそのまま物理コア数になる。
		// ただし共有コアが除かれた値が返ることがあり、それに合わせるとワーカーが足りなくなる。
		// 共有コアも使う前提なので、既知の構成を下限として扱う。
		SYSTEM_INFO systemInfo{};
		::GetNativeSystemInfo(&systemInfo);

		const uint32_t processorCount = static_cast<uint32_t>(systemInfo.dwNumberOfProcessors);

		return processorCount > XBOX_AVAILABLE_CORE_COUNT ? processorCount : XBOX_AVAILABLE_CORE_COUNT;
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
