/**
 * @file StatusPanel.cpp
 * @brief 実行中の値パネルの中身。行を文字列にして並べるだけ。
 */
#include "Pch.h"
#include "Editor/Panels/StatusPanel.h"
#include "Core/Status/StatusRow.h"
#include "Editor/EditorLayout.h"
#include <imgui.h>
#include <cfloat>


namespace fang::editor
{
	namespace
	{
		/** @brief 「実行中の値」ウィンドウの最小幅。レンダリング統計と同じ。 */
		constexpr float WINDOW_MIN_WIDTH = 420.0f;
	} // namespace


	bool StatusPanel::Initialize()
	{
		return true;
	}


	void StatusPanel::Shutdown() {}


	void StatusPanel::BuildFrame(const StatusRowList* statusRows)
	{
		ApplyPanelPlacement(EnPanelSlot::Status, /*isCollapsedAtStart*/ false);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin(STATUS_WINDOW_NAME, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		if (statusRows == nullptr)
		{
			ImGui::TextDisabled("この周の写しが無い（フレームメモリ不足か Release）");
			ImGui::End();
			return;
		}

		char text[MAX_STATUS_TEXT_LENGTH];
		for (const StatusRow& row : statusRows->GetRows())
		{
			if (row.kind == EnStatusRowKind::Separator)
			{
				ImGui::Separator();
				continue;
			}

			(void)FormatStatusRow(row, text);
			ImGui::TextUnformatted(text);
		}

		// 打ち切りは黙って消さない。0 のときも出して「切っていない」ことを見せる。
		ImGui::TextDisabled("行 %u 本 ／ 出せなかった行 %u", statusRows->rowCount, statusRows->droppedRowCount);

		ImGui::End();
	}
} // namespace fang::editor
