/**
 * @file RenderStatisticsPanel.cpp
 * @brief レンダリング統計パネルの中身。フレーム時間・FPS の移動平均と、Runtime から渡された 4 値の表示。
 */
#include "Pch.h"
#include "Editor/Panels/RenderStatisticsPanel.h"
#include "Runtime/FrameContext.h"
#include <imgui.h>


namespace fang::editor
{
	namespace
	{
		/** @brief 「レンダリング統計」ウィンドウの最小幅。自動サイズだけだと日本語ラベルの末尾が切れる。 */
		constexpr float WINDOW_MIN_WIDTH = 420.0f;

		/** @brief 初回に置く位置。既定のままだとジョブシステムウィンドウに重なる。 */
		constexpr ImVec2 FIRST_USE_POSITION{ 900.0f, 60.0f };
	} // namespace


	bool RenderStatisticsPanel::Initialize()
	{
		for (float& sample : m_frameTimeSecondsHistory)
		{
			sample = 0.0f;
		}

		m_frameTimeHistoryWriteIndex  = 0;
		m_frameTimeHistorySampleCount = 0;
		m_frameTimeMovingSumSeconds   = 0.0f;

		return true;
	}


	void RenderStatisticsPanel::Shutdown()
	{
		for (float& sample : m_frameTimeSecondsHistory)
		{
			sample = 0.0f;
		}

		m_frameTimeHistoryWriteIndex  = 0;
		m_frameTimeHistorySampleCount = 0;
		m_frameTimeMovingSumSeconds   = 0.0f;
	}


	void RenderStatisticsPanel::BuildFrame(float deltaTimeSeconds, const RenderStatistics& renderStatistics)
	{
		PushFrameTimeSample(deltaTimeSeconds);

		ImGui::SetNextWindowPos(FIRST_USE_POSITION, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin("レンダリング統計", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		BuildFrameTimeSection();
		ImGui::Separator();
		BuildRenderStatisticsSection(renderStatistics);

		ImGui::End();
	}


	void RenderStatisticsPanel::PushFrameTimeSample(float deltaTimeSeconds)
	{
		// 押し出す値を和から引く ➡ 新しい値を書いて和に足す ➡ 書き込み位置を進める、の O(1)。
		m_frameTimeMovingSumSeconds -= m_frameTimeSecondsHistory[m_frameTimeHistoryWriteIndex];
		m_frameTimeSecondsHistory[m_frameTimeHistoryWriteIndex] = deltaTimeSeconds;
		m_frameTimeMovingSumSeconds += deltaTimeSeconds;

		m_frameTimeHistoryWriteIndex = (m_frameTimeHistoryWriteIndex + 1) % FRAME_TIME_HISTORY_LENGTH;

		// 起動直後は履歴がまだ埋まっていないので、分母を実際に積んだ数へ絞る。フルの長さで割ると
		// 埋まっていない分の 0 に引きずられて、浅い平均が実際より甘い（短い）値になってしまう。
		if (m_frameTimeHistorySampleCount < FRAME_TIME_HISTORY_LENGTH)
		{
			++m_frameTimeHistorySampleCount;
		}
	}


	void RenderStatisticsPanel::BuildFrameTimeSection() const
	{
		const float averageFrameTimeSeconds =
			m_frameTimeHistorySampleCount > 0
				? m_frameTimeMovingSumSeconds / static_cast<float>(m_frameTimeHistorySampleCount)
				: 0.0f;

		const float averageFrameTimeMilliseconds = averageFrameTimeSeconds * 1000.0f;
		const float averageFramesPerSecond = averageFrameTimeSeconds > 0.0f ? 1.0f / averageFrameTimeSeconds : 0.0f;

		ImGui::Text(
			"フレーム時間: %.2f ms（移動平均 %u フレーム）",
			averageFrameTimeMilliseconds,
			FRAME_TIME_HISTORY_LENGTH
		);
		ImGui::Text("フレームレート: %.0f fps", averageFramesPerSecond);
	}


	void RenderStatisticsPanel::BuildRenderStatisticsSection(const RenderStatistics& renderStatistics) const
	{
		// Submit したうちの何個が実際に描かれたか（差がカリングで飛んだ数）を並べて読ませる。
		ImGui::Text("Submit した数: %u", renderStatistics.submittedItemCount);
		ImGui::Text("描いた数: %u", renderStatistics.drawnItemCount);
		ImGui::Text("地形の描画チャンク数: %u", renderStatistics.drawnTerrainChunkCount);
		ImGui::Separator();
		ImGui::Text("RenderGraph のパス数: %u", renderStatistics.passCount);
		ImGui::Text("コマンドリスト本数: %u", renderStatistics.commandListCount);
	}
} // namespace fang::editor
