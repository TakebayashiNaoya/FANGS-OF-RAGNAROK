/**
 * @file ImGuiPlatformInput.cpp
 * @brief ImGui への入力（Windows）。
 */
#include "Pch.h"
#include "ImGuiPlatformInput.h"
#include <imgui.h>
#include <windows.h>


namespace fang::editor
{
	void UpdateImGuiPlatformInput(void* windowHandle)
	{
		// TODO: WndProc をフックしてキー入力も拾う。今はマウスだけをポーリングしている。
		ImGuiIO& io = ImGui::GetIO();

		POINT cursorPosition{};
		if (::GetCursorPos(&cursorPosition) != FALSE &&
			::ScreenToClient(static_cast<HWND>(windowHandle), &cursorPosition) != FALSE)
		{
			io.AddMousePosEvent(static_cast<float>(cursorPosition.x), static_cast<float>(cursorPosition.y));
		}

		io.AddMouseButtonEvent(0, (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(1, (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(2, (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
	}
} // namespace fang::editor
