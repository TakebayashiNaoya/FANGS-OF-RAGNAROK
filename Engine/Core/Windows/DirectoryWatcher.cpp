/**
 * @file DirectoryWatcher.cpp
 * @brief ディレクトリの変化の見張りの実装（Windows）。
 */
#include "Pch.h"
#include "Core/Platform/DirectoryWatcher.h"
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


	DirectoryWatcher::~DirectoryWatcher()
	{
		Shutdown();
	}


	bool DirectoryWatcher::Initialize(const char* utf8DirectoryPath)
	{
		Shutdown();

		if (utf8DirectoryPath == nullptr || utf8DirectoryPath[0] == '\0')
		{
			return false;
		}

		const std::wstring widePath = ConvertUtf8ToWide(utf8DirectoryPath);
		if (widePath.empty())
		{
			return false;
		}

		// エディタによって保存の実現の仕方が違う（上書き / 一時ファイルを作って置き換え）ので、
		// 書き込み・追加削除・改名・大きさの変化をまとめて拾う。子ディレクトリは見ない。
		const HANDLE handle = ::FindFirstChangeNotificationW(
			widePath.c_str(),
			FALSE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE
		);
		if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
		{
			return false;
		}

		m_nativeHandle = handle;
		return true;
	}


	void DirectoryWatcher::Shutdown()
	{
		if (m_nativeHandle == nullptr)
		{
			return;
		}

		::FindCloseChangeNotification(m_nativeHandle);
		m_nativeHandle = nullptr;
	}


	bool DirectoryWatcher::ConsumeChange()
	{
		if (m_nativeHandle == nullptr)
		{
			return false;
		}

		// 待ち時間 0 なので、変化が無ければその場で戻る。
		if (::WaitForSingleObject(m_nativeHandle, 0) != WAIT_OBJECT_0)
		{
			return false;
		}

		// 次の変化を拾えるように合図を戻す。失敗したらハンドルが死んでいるので見張りを畳む。
		if (::FindNextChangeNotification(m_nativeHandle) == FALSE)
		{
			Shutdown();
		}

		return true;
	}
} // namespace fang
