/**
 * @file SystemFont.cpp
 * @brief システムフォントの探索（Xbox / UWP）。
 */
#include "Pch.h"
#include "Core/Platform/SystemFont.h"

// C++/WinRT(例外前提)を使ってよいのはこの Xbox ディレクトリの TU だけ。例外は外に出さない。
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <windows.h>


namespace fang
{
	std::string GetSystemUIFontPath()
	{
		// UWP からは C:\Windows\Fonts を読めないので、パッケージに同梱したフォント(OFL)を使う。
		// TODO: Resource 層ができたらそこから引く。
		try
		{
			const winrt::hstring installedLocation =
				winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path();

			std::wstring widePath{ std::wstring_view(installedLocation) };
			widePath += L"\\Assets\\Fonts\\MPLUS1p-Regular.ttf";

			if (::GetFileAttributesW(widePath.c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				return std::string();
			}

			// ImGui へは UTF-8 で渡す（ImGui 側が内部でワイド文字に戻して開く）。
			const int sizeInBytes =
				::WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (sizeInBytes <= 1)
			{
				return std::string();
			}

			std::string utf8Path(static_cast<size_t>(sizeInBytes) - 1, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, utf8Path.data(), sizeInBytes, nullptr, nullptr);
			return utf8Path;
		}
		catch (const winrt::hresult_error&)
		{
			return std::string();
		}
	}
} // namespace fang
