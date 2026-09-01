/**
 * @file ImGuiPlatformInput.h
 * @brief OS からマウスとキーの状態を取って ImGui に渡す。
 */
#pragma once

namespace fang::editor
{
	/**
	 * @brief ImGui の入力を 1 フレーム分更新する。
	 * @details 実装は Windows/ と Xbox/ にある。
	 * @threading メインスレッドのみ。
	 */
	void UpdateImGuiPlatformInput(void* windowHandle);
} // namespace fang::editor
