/**
 * @file Thread.cpp
 * @brief スレッドまわりの実装（Xbox / UWP）。使えるコア数の取得とスレッド名付け。
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


	uint32_t GetUsableCoreCount()
	{
		// OS に尋ねるとハード全体の 8 が返るが、それは「使える数」ではない。実機実測でワーカーが
		// 7 本になり、6 コアへの過剰予約になっていたため、既知の割り当てに固定する。
		return XBOX_AVAILABLE_CORE_COUNT;
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
