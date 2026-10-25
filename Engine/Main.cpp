/**
 * @file Main.cpp
 * @brief メイン関数の実装
 */
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include "Core/Memory/Memory.h"


namespace
{
	constexpr const wchar_t* WINDOW_CLASS_NAME = L"FangsOfRagnarok_MainWindow";
	constexpr const wchar_t* WINDOW_TITLE      = L"Fangs of Ragnarok";
	constexpr int            WINDOW_WIDTH      = 800;
	constexpr int            WINDOW_HEIGHT     = 600;

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


int main()
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
		return EXIT_FAILURE;
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
		return EXIT_FAILURE;
	}

	ShowWindow(window, SW_SHOW);

	Memory memory;
	void* ptr1 = memory.MallocMemory(64);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	void* ptr2 = memory.MallocMemory(32);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	memory.FreeMemory(ptr1);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	memory.FreeMemory(ptr2);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	return 0;
}
