/**
 * @file ShaderReloadPanel.cpp
 * @brief シェーダーホットリロードパネルの中身。見張りの状態と、直近の作り直しの成否の表示。
 */
#include "Pch.h"
#include "Editor/Panels/ShaderReloadPanel.h"
#include "RHI/ShaderHotReload.h"
#include <imgui.h>


namespace fang::editor
{
	namespace
	{
		/** @brief 「シェーダーホットリロード」ウィンドウの最小幅。エラー文が 1 語ずつに折られると読めない。 */
		constexpr float WINDOW_MIN_WIDTH = 480.0f;

		/** @brief 初回に置く位置。既定のままだとレンダリング統計に重なる。 */
		constexpr ImVec2 FIRST_USE_POSITION{ 900.0f, 320.0f };

		/** @brief 失敗しているときの文字色。 */
		constexpr ImVec4 FAILURE_COLOR{ 1.0f, 0.4f, 0.4f, 1.0f };
	} // namespace


	bool ShaderReloadPanel::Initialize(const rhi::ShaderReloadStatus* status)
	{
		m_status = status;
		return true;
	}


	void ShaderReloadPanel::Shutdown()
	{
		m_status = nullptr;
	}


	void ShaderReloadPanel::BuildFrame() const
	{
		ImGui::SetNextWindowPos(FIRST_USE_POSITION, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin("シェーダーホットリロード", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		if (m_status == nullptr)
		{
			ImGui::TextUnformatted("この構成では使えない");
			ImGui::End();
			return;
		}

		// 見張りが立っていないと、保存しても何も起きない ➡ まずここを見せる。
		ImGui::Text("見張り: %s", m_status->isWatching ? "あり（.hlsl の保存で作り直す）" : "なし");
		ImGui::Text("成功: %u 回 / 失敗: %u 回", m_status->successCount, m_status->failureCount);

		if (m_status->lastMessage[0] != '\0')
		{
			ImGui::Separator();

			// エラー文は数行になるので、ウィンドウの幅で折り返す。
			ImGui::PushTextWrapPos(0.0f);
			if (m_status->wasLastAttemptSuccessful)
			{
				ImGui::TextUnformatted(m_status->lastMessage);
			}
			else
			{
				ImGui::TextColored(FAILURE_COLOR, "%s", m_status->lastMessage);
			}

			ImGui::PopTextWrapPos();
		}

		ImGui::End();
	}
} // namespace fang::editor
