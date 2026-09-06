/**
 * @file TuningPanel.cpp
 * @brief 登録された調整値を、入れ子も含めてつまみへ写すパネルの中身。
 */
#include "Pch.h"
#include "Editor/Panels/TuningPanel.h"
#include "Core/Reflection/TuningRegistry.h"
#include <imgui.h>
#include <cfloat>


namespace fang::editor
{
	namespace
	{
		/** @brief 初回に置く位置。左が予算、上がジョブシステム、右の列は描画統計とシェーダー。 */
		constexpr ImVec2 FIRST_USE_POSITION{ 460.0f, 330.0f };

		constexpr float WINDOW_MIN_WIDTH  = 400.0f;
		constexpr float WINDOW_MAX_HEIGHT = 600.0f;
		constexpr float ITEM_WIDTH        = 220.0f;
	} // namespace


	bool TuningPanel::Initialize()
	{
		return true;
	}


	void TuningPanel::Shutdown() {}


	void TuningPanel::BuildFrame()
	{
		ImGui::SetNextWindowPos(FIRST_USE_POSITION, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, WINDOW_MAX_HEIGHT));

		if (!ImGui::Begin("調整値", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			// 畳んでいるフレームはここで戻る ➡ 行の組み立ても走らない。
			ImGui::End();
			return;
		}

		TuningRegistry&                    registry = TuningRegistry::GetInstance();
		const std::span<const TuningEntry> entries  = registry.GetEntries();

		m_buildResult = registry.BuildRows(m_rows);

		uint32_t rowIndex = 0;
		for (uint32_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
		{
			const bool isOpen =
				ImGui::CollapsingHeader(entries[entryIndex].displayName, ImGuiTreeNodeFlags_DefaultOpen);

			uint32_t         indentLevel = 0;
			const TuningRow* previousRow = nullptr;

			while (rowIndex < m_buildResult.rowCount && m_rows[rowIndex].entryIndex == entryIndex)
			{
				const TuningRow& row = m_rows[rowIndex];
				if (isOpen)
				{
					// 前の行と共通する段の数を数える。同じ FieldInfo を通っているので名前はポインタで比べてよい。
					uint32_t sharedCount = 0;
					while (previousRow != nullptr && sharedCount + 1 < row.depth &&
						   sharedCount + 1 < previousRow->depth &&
						   row.segments[sharedCount].name == previousRow->segments[sharedCount].name)
					{
						++sharedCount;
					}

					while (indentLevel > sharedCount)
					{
						ImGui::Unindent();
						--indentLevel;
					}

					// 入れ子の見出しを出してから 1 段下げる ➡ 段の深さがそのまま画面の字下げになる。
					while (indentLevel + 1 < row.depth)
					{
						ImGui::SeparatorText(row.segments[indentLevel].displayName);
						ImGui::Indent();
						++indentLevel;
					}

					BuildRowWidget(row);
					previousRow = &row;
				}

				++rowIndex;
			}

			while (indentLevel > 0)
			{
				ImGui::Unindent();
				--indentLevel;
			}
		}

		// 打ち切りは黙って消さない。0 のときも出して「切っていない」ことを見せる。
		ImGui::Separator();
		ImGui::TextDisabled(
			"行 %u 本 ／ 段の上限で出さなかったフィールド %u ／ 置き場が足りず出せなかった行 %u ／ "
			"取りこぼした書き戻し %u",
			m_buildResult.rowCount,
			m_buildResult.depthLimitedFieldCount,
			m_buildResult.droppedRowCount,
			registry.GetDroppedWriteCount()
		);

		ImGui::End();
	}


	void TuningPanel::BuildRowWidget(const TuningRow& row) const
	{
		FieldValue value;
		if (!ReadFieldValue(row.address, row.type, &value))
		{
			return;
		}

		// 同じ表示名の行が別の実体に出る（狼の牙と雑魚の牙）ので、ID は実体のアドレスで分ける。
		ImGui::PushID(row.address);
		ImGui::SetNextItemWidth(ITEM_WIDTH);

		bool didEdit = false;
		switch (row.type)
		{
			case EnFieldType::Float:
				didEdit = row.range.hasRange ? ImGui::SliderFloat(
												   row.GetDisplayName(),
												   &value.floatValue,
												   row.range.minValue,
												   row.range.maxValue
											   )
											 : ImGui::DragFloat(row.GetDisplayName(), &value.floatValue);
				break;

			case EnFieldType::Int32:
				didEdit = row.range.hasRange ? ImGui::SliderInt(
												   row.GetDisplayName(),
												   &value.int32Value,
												   static_cast<int>(row.range.minValue),
												   static_cast<int>(row.range.maxValue)
											   )
											 : ImGui::DragInt(row.GetDisplayName(), &value.int32Value);
				break;

			case EnFieldType::Bool: didEdit = ImGui::Checkbox(row.GetDisplayName(), &value.boolValue); break;

			case EnFieldType::Struct: break; // 入れ子は行にならないのでここへは来ない。
		}

		if (didEdit)
		{
			// 実体へは書かない。更新ジョブが走っていない時点でフレームループが入れる。
			(void)TuningRegistry::GetInstance().EnqueueWrite(row, value);
		}

		ImGui::PopID();
	}
} // namespace fang::editor
