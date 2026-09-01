/**
 * @file ImGuiPlatformInput.cpp
 * @brief ImGui への入力（Xbox / UWP）。
 */
#include "Pch.h"
#include "Editor/ImGuiPlatformInput.h"
#include "Core/CoreMacros.h"


namespace fang::editor
{
	void UpdateImGuiPlatformInput(void* windowHandle)
	{
		// TODO: Windows.Gaming.Input のパッドで動かす。Preview の実機調整に要る。
		FANG_UNUSED(windowHandle);
	}
} // namespace fang::editor
