/**
 * @file UWPApplication.cpp
 * @brief UWP のアプリモデル(CoreApplication / IFrameworkView)の実装。
 */
#include "Pch.h"
#include "Core/Platform/UWPApplication.h"
#include "Core/CoreLog.h"

// C++/WinRT(例外前提)を使ってよいのはこの Xbox ディレクトリの TU だけ(規約 9)。例外は外に出さない。
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>


namespace fang
{
	namespace
	{
		namespace winrt_core = winrt::Windows::ApplicationModel::Core;
		namespace winrt_ui   = winrt::Windows::UI::Core;

		/** @brief OS が持つループ(Run)の中からゲーム本体を呼び返すための橋。 */
		struct FrameworkView : winrt::implements<FrameworkView, winrt_core::IFrameworkView>
		{
			int (*runGame)() = nullptr;
			int exitCode     = 0;

			void Initialize(const winrt_core::CoreApplicationView& applicationView)
			{
				// Activate しないとスプラッシュ画面のまま止まる。
				applicationView.Activated([](auto&&, auto&&) { winrt_ui::CoreWindow::GetForCurrentThread().Activate(); });
			}

			void SetWindow(const winrt_ui::CoreWindow&) {}
			void Load(const winrt::hstring&) {}
			void Uninitialize() {}

			void Run() { exitCode = runGame(); }
		};

		struct FrameworkViewSource : winrt::implements<FrameworkViewSource, winrt_core::IFrameworkViewSource>
		{
			int (*runGame)() = nullptr;
			winrt::com_ptr<FrameworkView> view;

			winrt_core::IFrameworkView CreateView()
			{
				view          = winrt::make_self<FrameworkView>();
				view->runGame = runGame;
				return *view;
			}
		};
	} // namespace

	int RunUWPApplication(int (*runGame)())
	{
		try
		{
			// MTA で初期化しないと CoreApplication::Run が hresult_wrong_thread で落ちる。
			winrt::init_apartment();

			auto source     = winrt::make_self<FrameworkViewSource>();
			source->runGame = runGame;
			winrt_core::CoreApplication::Run(*source);

			return source->view != nullptr ? source->view->exitCode : 0;
		}
		catch (const winrt::hresult_error& error)
		{
			FANG_LOG_ERROR(Core, "UWP の起動に失敗した (HRESULT=0x{:08X})", static_cast<uint32_t>(error.code().value));
			return 1;
		}
	}
} // namespace fang
