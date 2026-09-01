/**
 * @file MainUWP.cpp
 * @brief UWP のエントリポイント。
 */
#include "Core/Platform/UWPApplication.h"
#include "GameMain.h"


// プラットフォームのエントリポイントなので、ここだけ <windows.h> を許す。
#include <windows.h>


int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
	// UWP はループを OS が持つので、CoreApplication の中からゲーム本体を呼び返してもらう。
	return fang::RunUWPApplication(&fang::game::Run);
}
