/**
 * @file Application.cpp
 * @brief アプリケーションクラスの実装
 */
#include "Application.h"
#include <iostream>
#include <chrono>


namespace fangEngine
{
	namespace
	{
		constexpr const wchar_t* WINDOW_CLASS_NAME   = L"FangsOfRagnarok_MainWindow"; // ウィンドウクラス名
		constexpr const wchar_t* WINDOW_TITLE        = L"Fangs of Ragnarok";          // ウィンドウ上部に表示されるタイトル
		constexpr int            WINDOW_WIDTH        = 800;
		constexpr int            WINDOW_HEIGHT       = 600;

		constexpr float MAX_DELTA_TIME = 1.0f / 30.0f;

		/**
		 * @brief WM_DESTROYの場合はループを抜ける指示を出し、それ以外の場合はWindowsの既定の処理に任せる
		 * @return WM_DESTROYの場合は0を返し、それ以外の場合はDefWindowProcWの戻り値を返す
		 */
		LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
		{
			// ImGuiが使ったメッセージは、ここで終わりにする
			if (render::DebugOverlay::HandleMessage(window, message, wParam, lParam)) return true;

			switch (message)
			{
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProcW(window, message, wParam, lParam);
			}
		}
	}


	Application::~Application()
	{
		// DebugOverlayが捨てるものをGPUが読み終えるのを待つ
		m_renderContext.WaitForGpu();
	}


	bool Application::Initialize()
	{
		HWND window = CreateMainWindow();
		if (window == nullptr) return false;
		if (m_renderContext.Initialize(window) == false)                 return false;
		if (m_debugOverlay.Initialize(window, m_renderContext) == false) return false;
		return true;
	}


	bool Application::Run()
	{
		MSG message{};
		auto previousFrameTime = std::chrono::steady_clock::now();
		bool running = true;
		while (running)
		{
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT) {
					running = false;
					break;
				}
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}

			// ウィンドウが閉じられた場合はループを抜ける
			if (running == false) break;

			// 時間の更新処理
			auto currentFrameTime = std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
			if (deltaTime > MAX_DELTA_TIME) {
				deltaTime = MAX_DELTA_TIME;
			}
			previousFrameTime = currentFrameTime;

			// 描画の開始
			if (m_renderContext.BeginFrame() == false) return false;
			// デバッグ情報の描画
			m_debugOverlay.Render(m_renderContext.GetCommandList());
			// 描画の終了
			if (m_renderContext.EndFrame() == false)   return false;
		}

		return true;
	}


	HWND Application::CreateMainWindow()
	{
		HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass{};
		windowClass.cbSize        = sizeof(WNDCLASSEXW);
		windowClass.lpfnWndProc   = WindowProcedure;
		windowClass.hInstance     = instance;
		windowClass.lpszClassName = WINDOW_CLASS_NAME;
		ATOM atom = RegisterClassExW(&windowClass);

		if (atom == 0) {
			std::cerr << "ウィンドウクラスの登録に失敗しました。" << std::endl;
			return nullptr;
		}

		HWND window = CreateWindowExW(
			0,
			WINDOW_CLASS_NAME,
			WINDOW_TITLE,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			WINDOW_WIDTH,
			WINDOW_HEIGHT,
			nullptr,
			nullptr,
			instance,
			nullptr
		);

		if (window == nullptr) {
			std::cerr << "ウィンドウの作成に失敗しました。" << std::endl;
			return nullptr;
		}

		ShowWindow(window, SW_SHOW);

		return window;
	}
} // namespace fangEngine
