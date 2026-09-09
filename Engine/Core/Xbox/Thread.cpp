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

		/**
		 * @brief Game 分類のアプリに割り当てられるコア数。
		 * @details 占有 4 + 共有 2。メモリ予算をまとめる置き場ができたらそちらへ移す。
		 */
		constexpr uint32_t USABLE_CORE_COUNT = 6;
	} // namespace


	uint32_t GetUsableCoreCount()
	{
		// OS に尋ねるとハード全体の 8 が返るが、それは「使える数」ではない。
		// 8 を採るとワーカーが 6 コアへの過剰予約になるので、既知の割り当てに固定する。
		return USABLE_CORE_COUNT;
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
