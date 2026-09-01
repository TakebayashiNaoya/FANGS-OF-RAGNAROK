/**
 * @file SystemFont.cpp
 * @brief システムフォントの探索（Windows）。
 */
#include "Pch.h"
#include "Core/Platform/SystemFont.h"
#include "Core/CoreMacros.h"
#include <windows.h>


namespace fang
{
	namespace
	{
		// 見た目が良い順。どれも Windows 10 以降なら入っている。
		constexpr const wchar_t* CANDIDATE_FONT_FILE_NAMES[] = {
			L"YuGothM.ttc",
			L"meiryo.ttc",
			L"msgothic.ttc",
		};
	} // namespace

	std::string GetSystemUIFontPath()
	{
		wchar_t    fontsDirectory[MAX_PATH]{};
		const UINT length = ::GetWindowsDirectoryW(fontsDirectory, MAX_PATH);
		if (length == 0)
		{
			return std::string();
		}

		for (const wchar_t* fileName : CANDIDATE_FONT_FILE_NAMES)
		{
			std::wstring path(fontsDirectory, length);
			path += L"\\Fonts\\";
			path += fileName;

			if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				continue;
			}

			const int byteCount = ::WideCharToMultiByte(
				CP_UTF8,
				0,
				path.c_str(),
				static_cast<int>(path.size()),
				nullptr,
				0,
				nullptr,
				nullptr
			);
			if (byteCount <= 0)
			{
				continue;
			}

			std::string utf8Path(static_cast<size_t>(byteCount), '\0');
			::WideCharToMultiByte(
				CP_UTF8,
				0,
				path.c_str(),
				static_cast<int>(path.size()),
				utf8Path.data(),
				byteCount,
				nullptr,
				nullptr
			);

			return utf8Path;
		}

		return std::string();
	}
} // namespace fang
