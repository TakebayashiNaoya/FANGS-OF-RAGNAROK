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

		// ソースツリーの根っこの目印。リポジトリの一番上にしか無い。
		constexpr const wchar_t* SOLUTION_FILE_NAME = L"FangsOfRagnarok.sln";

		/** @brief UTF-16 の文字列を UTF-8 に直す。変換できなければ空文字列。 */
		std::string ToUtf8(const std::wstring& widePath)
		{
			const int byteCount = ::WideCharToMultiByte(
				CP_UTF8,
				0,
				widePath.c_str(),
				static_cast<int>(widePath.size()),
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
				widePath.c_str(),
				static_cast<int>(widePath.size()),
				utf8Path.data(),
				byteCount,
				nullptr,
				nullptr
			);

			return utf8Path;
		}

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

		return ToUtf8(rootPath);
	}


	std::string GetSourceRootPath()
	{
		std::wstring directoryPath = GetExecutableFilePath();
		if (directoryPath.empty())
		{
			return std::string();
		}

		// exe は Bin\x64\<構成>\ に出る ➡ リポジトリの中なので、上へ辿れば必ずソリューションに当たる。
		// 外へコピーした exe では当たらないが、そのときはソースを読む機能が黙って止まるだけで描画は変わらない。
		while (true)
		{
			const size_t separatorIndex = directoryPath.find_last_of(L'\\');
			if (separatorIndex == std::wstring::npos)
			{
				return std::string();
			}

			directoryPath.resize(separatorIndex);

			std::wstring solutionPath = directoryPath;
			solutionPath += L'\\';
			solutionPath += SOLUTION_FILE_NAME;

			const DWORD attributes = ::GetFileAttributesW(solutionPath.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				return ToUtf8(directoryPath);
			}
		}
	}
} // namespace fang
