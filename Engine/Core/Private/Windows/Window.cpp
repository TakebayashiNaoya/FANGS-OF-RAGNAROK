/**
 * @file Window.cpp
 * @brief ウィンドウの実装（Windows）。HWND とメッセージループ。
 */
#include "Pch.h"
#include "Core/Platform/Window.h"
#include "Core/CoreLog.h"
#include "Core/Log/Assert.h"
#include <windows.h>


namespace fang
{
	namespace
	{
		constexpr const wchar_t* WINDOW_CLASS_NAME = L"FangEngineWindow";

		LRESULT CALLBACK WindowProcedure(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
		{
			// ウィンドウごとの状態は生成時に GWLP_USERDATA へ入れておく。
			Window* window = reinterpret_cast<Window*>(::GetWindowLongPtrW(windowHandle, GWLP_USERDATA));

			switch (message)
			{
				case WM_CREATE:
				{
					const CREATESTRUCTW* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
					::SetWindowLongPtrW(windowHandle,
										GWLP_USERDATA,
										reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
					return 0;
				}

				case WM_SIZE:
				{
					// 最小化は 0x0 で来る。作り直せないので無視する。
					const uint32_t width  = static_cast<uint32_t>(LOWORD(lParam));
					const uint32_t height = static_cast<uint32_t>(HIWORD(lParam));
					if (window != nullptr && width > 0 && height > 0)
					{
						window->OnResized(width, height);
					}

					return 0;
				}

				case WM_CLOSE:
				{
					::DestroyWindow(windowHandle);
					return 0;
				}

				case WM_DESTROY:
				{
					::PostQuitMessage(0);
					return 0;
				}

				default: break;
			}

			return ::DefWindowProcW(windowHandle, message, wParam, lParam);
		}
	} // namespace

	Window::~Window()
	{
		Shutdown();
	}

	bool Window::Initialize(const WindowDesc& desc)
	{
		FANG_ASSERT(m_nativeHandle == nullptr, "ウィンドウを二重に初期化している");

		const HINSTANCE instanceHandle = ::GetModuleHandleW(nullptr);

		WNDCLASSEXW windowClass{};
		windowClass.cbSize        = sizeof(windowClass);
		windowClass.style         = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc   = WindowProcedure;
		windowClass.hInstance     = instanceHandle;
		windowClass.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
		windowClass.lpszClassName = WINDOW_CLASS_NAME;

		// 2 回目以降は登録済みなので、その失敗だけは無視する。
		if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		{
			FANG_LOG_ERROR(Core, "ウィンドウクラスの登録に失敗した (GetLastError={})", ::GetLastError());
			return false;
		}

		RECT windowRect{ 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
		::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		const HWND windowHandle = ::CreateWindowExW(0,
													WINDOW_CLASS_NAME,
													desc.title,
													WS_OVERLAPPEDWINDOW,
													CW_USEDEFAULT,
													CW_USEDEFAULT,
													windowRect.right - windowRect.left,
													windowRect.bottom - windowRect.top,
													nullptr,
													nullptr,
													instanceHandle,
													this);

		if (windowHandle == nullptr)
		{
			FANG_LOG_ERROR(Core, "ウィンドウの生成に失敗した (GetLastError={})", ::GetLastError());
			return false;
		}

		m_nativeHandle     = windowHandle;
		m_width            = desc.width;
		m_height           = desc.height;
		m_isCloseRequested = false;

		::ShowWindow(windowHandle, SW_SHOW);
		FANG_LOG_INFO(Core, "ウィンドウを作った ({}x{})", desc.width, desc.height);

		return true;
	}

	void Window::Shutdown()
	{
		if (m_nativeHandle == nullptr)
		{
			return;
		}

		::DestroyWindow(static_cast<HWND>(m_nativeHandle));
		m_nativeHandle = nullptr;
	}

	bool Window::PumpMessages()
	{
		MSG message{};
		while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
		{
			if (message.message == WM_QUIT)
			{
				m_isCloseRequested = true;
			}

			::TranslateMessage(&message);
			::DispatchMessageW(&message);
		}

		return !m_isCloseRequested;
	}

	void Window::OnResized(uint32_t width, uint32_t height)
	{
		if (m_width == width && m_height == height)
		{
			return;
		}

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
