/**
 * @file Window.cpp
 * @brief ウィンドウの実装(Xbox / UWP)。CoreWindow。
 */
#include "Pch.h"
#include "Core/Platform/Window.h"
#include "Core/CoreLog.h"

// C++/WinRT(例外前提)を使ってよいのはこの Xbox ディレクトリの TU だけ(規約 9)。例外は外に出さない。
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.UI.Core.h>


namespace fang
{
	namespace
	{
		namespace winrt_ui      = winrt::Windows::UI::Core;
		namespace winrt_display = winrt::Windows::Graphics::Display;

		// CoreWindow はスレッドに 1 枚しか無いので、購読の記録はここで持つ。
		winrt_ui::CoreWindow s_coreWindow{ nullptr };
		winrt::event_token s_sizeChangedToken{};
		winrt::event_token s_closedToken{};

		/** @brief 論理ピクセル(DIP)を物理ピクセルに直す。 */
		uint32_t ToPhysicalPixels(float deviceIndependentPixels, double scale)
		{
			return static_cast<uint32_t>(deviceIndependentPixels * scale + 0.5);
		}

		/** @brief 今の表示スケール。Xbox はテレビ向けに 2 倍などになっている。 */
		double GetDisplayScale()
		{
			return winrt_display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
		}
	} // namespace

	Window::~Window()
	{
		Shutdown();
	}

	bool Window::Initialize(const WindowDesc& desc)
	{
		// 大きさは CoreWindow(全画面)が決め、タイトルはマニフェストが決める。desc は使わない。
		FANG_UNUSED(desc);

		try
		{
			winrt_ui::CoreWindow window = winrt_ui::CoreWindow::GetForCurrentThread();
			if (window == nullptr)
			{
				FANG_LOG_ERROR(Core, "CoreWindow が無い。RunUWPApplication の中から呼ぶこと");
				return false;
			}

			const double scale = GetDisplayScale();
			m_width            = ToPhysicalPixels(window.Bounds().Width, scale);
			m_height           = ToPhysicalPixels(window.Bounds().Height, scale);

			// 参照カウントは増やさない借用。CoreWindow はアプリと同寿命なのでぶら下がらない。
			m_nativeHandle = winrt::get_unknown(window);

			s_sizeChangedToken =
				window.SizeChanged([this](const winrt_ui::CoreWindow& sender, const winrt_ui::WindowSizeChangedEventArgs&)
			{
				const double newScale = GetDisplayScale();
				OnResized(ToPhysicalPixels(sender.Bounds().Width, newScale),
						  ToPhysicalPixels(sender.Bounds().Height, newScale));
			});

			s_closedToken = window.Closed([this](auto&&, auto&&) { m_isCloseRequested = true; });

			s_coreWindow = window;

			FANG_LOG_INFO(Core, "CoreWindow を取得した ({}x{})", m_width, m_height);
			return true;
		}
		catch (const winrt::hresult_error& error)
		{
			FANG_LOG_ERROR(Core,
						   "CoreWindow の初期化に失敗した (HRESULT=0x{:08X})",
						   static_cast<uint32_t>(error.code().value));
			return false;
		}
	}

	void Window::Shutdown()
	{
		if (s_coreWindow != nullptr)
		{
			s_coreWindow.SizeChanged(s_sizeChangedToken);
			s_coreWindow.Closed(s_closedToken);
			s_coreWindow = nullptr;
		}

		m_nativeHandle = nullptr;
	}

	bool Window::PumpMessages()
	{
		if (s_coreWindow == nullptr)
		{
			return false;
		}

		// TODO: PLM(Suspending で IDXGIDevice3::Trim)は実機で復帰を確かめるときに入れる。
		try
		{
			s_coreWindow.Dispatcher().ProcessEvents(winrt_ui::CoreProcessEventsOption::ProcessAllIfPresent);
		}
		catch (const winrt::hresult_error&)
		{
			return false;
		}

		return !m_isCloseRequested;
	}

	void Window::OnResized(uint32_t width, uint32_t height)
	{
		m_width         = width;
		m_height        = height;
		m_isSizeChanged = true;
	}

	bool Window::ConsumeSizeChange()
	{
		const bool wasChanged = m_isSizeChanged;
		m_isSizeChanged       = false;
		return wasChanged;
	}
} // namespace fang
