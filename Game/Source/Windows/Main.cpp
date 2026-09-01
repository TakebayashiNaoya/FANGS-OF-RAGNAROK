/**
 * @file Main.cpp
 * @brief Win32 のエントリポイント。
 */
#include "GameMain.h"


// wWinMain の署名に要るので直接入れる。
// RHI 経由でも（GraphicsDevice.h ➡ d3d12.h で）入ってくるが、この TU は RHI を見ないので自前で。
#include <windows.h>


int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
	return fang::game::Run();
}
