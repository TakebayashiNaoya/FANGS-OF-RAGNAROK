/**
 * @file EditorLayout.cpp
 * @brief パネルの席から座標を求め、次の Begin に効かせる。
 */
#include "Pch.h"
#include "Editor/EditorLayout.h"
#include <imgui.h>


namespace fang::editor
{
	namespace
	{
		constexpr float PANEL_LEFT_X  = 16.0f; /**< 6 枚とも揃える左端。 */
		constexpr float PANEL_FIRST_Y = 56.0f; /**< 先頭の席の y。左上の行き先オーバーレイ（y 16〜50）の下。 */

		/** @brief 席 1 つぶんの縦の間隔。畳んだ 1 枚の高さ 24px（フォント 18 + FramePadding.y 3 x 2）+ 隙間 10px。 */
		constexpr float PANEL_STRIDE_Y = 34.0f;
	} // namespace


	void ApplyPanelPlacement(EnPanelSlot slot)
	{
		const float y = PANEL_FIRST_Y + PANEL_STRIDE_Y * static_cast<float>(static_cast<uint32_t>(slot));

		ImGui::SetNextWindowPos(ImVec2(PANEL_LEFT_X, y), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
	}
} // namespace fang::editor
