/**
 * @file FileSystem.cpp
 * @brief ファイルを開く処理の実装（Xbox / UWP）。
 */
#include "Pch.h"
#include "Core/Platform/FileSystem.h"
#include <windows.h>
#include <string>


namespace fang
{
	namespace
	{
		/**
		 * @brief UTF-8 の文字列を UTF-16 に直す。
		 * @return 変換できなければ空文字列。
		 */
		[[nodiscard]] std::wstring ConvertUtf8ToWide(const char* utf8Text)
		{
			const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, utf8Text, -1, nullptr, 0);
			if (wideLength <= 0)
			{
				return std::wstring();
			}

			// wideLength は終端の '\0' を含むので、確保は 1 少なくして c_str() の分と帳尻を合わせる。
			std::wstring wideText(static_cast<size_t>(wideLength) - 1, L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, utf8Text, -1, wideText.data(), wideLength);
			return wideText;
		}
	} // namespace


	std::FILE* OpenFile(const char* utf8Path, const char* mode)
	{
		if (utf8Path == nullptr || utf8Path[0] == '\0' || mode == nullptr || mode[0] == '\0')
		{
			return nullptr;
		}

		const std::wstring widePath = ConvertUtf8ToWide(utf8Path);
		const std::wstring wideMode = ConvertUtf8ToWide(mode);
		if (widePath.empty() || wideMode.empty())
		{
			return nullptr;
		}

		// _wfopen_s は WinRT を介さない CRT の関数なので、パッケージの Assets 配下を読むだけの
		// この用途なら UWP のアプリコンテナからでも問題なく使える。
		std::FILE* file = nullptr;
		if (_wfopen_s(&file, widePath.c_str(), wideMode.c_str()) != 0)
		{
			return nullptr;
		}

		return file;
	}
} // namespace fang
