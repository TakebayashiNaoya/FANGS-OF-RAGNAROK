/**
 * @file AssetPath.cpp
 * @brief アセットの置き場所の解決（Windows）。
 */
#include "Pch.h"
#include "Core/Platform/AssetPath.h"
#include <windows.h>


namespace fang
{
	namespace
	{
		// \\?\ を付けたときのパスの上限。これを超えて伸ばしても OS が受け付けない。
		constexpr size_t MAX_EXTENDED_PATH_LENGTH = 32768;

		/** @brief 実行ファイルのフルパスを返す。取れなければ空文字列。 */
		std::wstring GetExecutableFilePath()
		{
			std::wstring path(MAX_PATH, L'\0');
			while (path.size() <= MAX_EXTENDED_PATH_LENGTH)
			{
				// 成功しても直前のエラーが消えない環境があるので、自分で均してから呼ぶ。
				::SetLastError(ERROR_SUCCESS);

				const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
				if (length == 0)
				{
					return std::wstring();
				}

				// 切り詰められたときだけ ERROR_INSUFFICIENT_BUFFER が残る。倍にして取り直す。
				if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				{
					path.resize(length);
					return path;
				}

				path.resize(path.size() * 2);
			}

			return std::wstring();
		}
	} // namespace


	std::string GetAssetRootPath()
	{
		const std::wstring executablePath = GetExecutableFilePath();
		if (executablePath.empty())
		{
			return std::string();
		}

		const size_t separatorIndex = executablePath.find_last_of(L'\\');
		if (separatorIndex == std::wstring::npos)
		{
			return std::string();
		}

		// exe の隣。ビルド後のコピー先と配布物の中で同じ位置に来る。
		std::wstring rootPath = executablePath.substr(0, separatorIndex);
		rootPath += L"\\Assets";

		const int byteCount = ::WideCharToMultiByte(
			CP_UTF8,
			0,
			rootPath.c_str(),
			static_cast<int>(rootPath.size()),
			nullptr,
			0,
			nullptr,
			nullptr
		);
		if (byteCount <= 0)
		{
			return std::string();
		}

		std::string utf8Path(static_cast<size_t>(byteCount), '\0');
		::WideCharToMultiByte(
			CP_UTF8,
			0,
			rootPath.c_str(),
			static_cast<int>(rootPath.size()),
			utf8Path.data(),
			byteCount,
			nullptr,
			nullptr
		);

		return utf8Path;
	}
} // namespace fang
