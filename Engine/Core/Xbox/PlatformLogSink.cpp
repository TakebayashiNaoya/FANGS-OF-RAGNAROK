/**
 * @file PlatformLogSink.cpp
 * @brief ログの出力先（Xbox / UWP）。デバッガ出力と LocalState/startup.log の両方へ流す。
 */
#include "Pch.h"
#include "Core/Log/PlatformLogSink.h"

// C++/WinRT（例外前提）を使ってよいのはこの Xbox ディレクトリの TU だけ。例外は外に出さない。
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <windows.h>
#include <cstdio>
#include <string>


namespace fang
{
	namespace
	{
		/**
		 * @brief LocalState/startup.log を書き込みモードで開く。失敗したら nullptr。
		 * @details 実機にはデバッガが付かないので、起動診断はこのファイルで行う。読み出しは
		 *          Device Portal ➡ File explorer ➡ 対象アプリ ➡ LocalState/。
		 *          追記でなく上書きで開く ➡ 常に「今回の起動」のログだけが残り、前回と混ざらない。
		 */
		[[nodiscard]] std::FILE* OpenStartupLog()
		{
			try
			{
				const winrt::hstring localFolder =
					winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path();

				std::wstring widePath{ std::wstring_view(localFolder) };
				widePath += L"\\startup.log";

				std::FILE* file = nullptr;
				if (::_wfopen_s(&file, widePath.c_str(), L"w") != 0)
				{
					return nullptr;
				}

				return file;
			}
			catch (const winrt::hresult_error&)
			{
				return nullptr;
			}
		}
	} // namespace


	void WriteLogToPlatform(std::string_view line)
	{
		// OutputDebugStringA は null 終端が要るので詰め直す。
		const std::string terminated(line);
		::OutputDebugStringA(terminated.c_str());

		// 関数ローカル static の初期化はスレッド安全。開けなければデバッガ出力だけになる。
		static std::FILE* s_logFile = OpenStartupLog();
		if (s_logFile != nullptr)
		{
			std::fputs(terminated.c_str(), s_logFile);

			// 直後に落ちても最後の行が残るよう、毎回フラッシュする。起動診断が目的なので速度より確実さ。
			std::fflush(s_logFile);
		}
	}
} // namespace fang
