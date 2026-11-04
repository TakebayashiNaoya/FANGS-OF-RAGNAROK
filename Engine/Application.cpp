/**
 * @file Application.cpp
 * @brief アプリケーションクラスの実装
 */
#include "Application.h"
#include <iostream>


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


bool Application::Initialize()
{
	HWND window = CreateMainWindow();
	if (window == nullptr) return false;
	return m_renderContext.Initialize(window);
}


bool Application::Run()
{
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
