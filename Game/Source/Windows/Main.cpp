/**
 * @file Main.cpp
 * @brief Win32 のエントリポイント。
 */
#include "GameMain.h"


// プラットフォームのエントリポイントなので、ここだけ <windows.h> を許す。
#include <windows.h>


int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
	return fang::game::Run();
}
