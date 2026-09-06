/**
 * @file RenderStatisticsPanel.cpp
 * @brief レンダリング統計パネルの中身。件数の表示と、フレーム時間・描画の内訳・パス別 GPU 時間の移動平均。
 */
#include "Pch.h"
#include "Editor/Panels/RenderStatisticsPanel.h"
#include "Editor/EditorLayout.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>


namespace fang::editor
{
	namespace
	{
		/** @brief 「レンダリング統計」ウィンドウの最小幅。自動サイズだけだと日本語ラベルの末尾が切れる。 */
		constexpr float WINDOW_MIN_WIDTH = 420.0f;

#if FANG_ENABLE_PROFILER
		/** @brief パス名の右に ms を置く桁。名前の長さがまちまちでも数字の頭が揃うように固定する。 */
		constexpr float PASS_TIME_COLUMN_OFFSET = 200.0f;
#endif
	} // namespace


	void RenderStatisticsPanel::MovingAverage::Push(float value)
	{
		movingSum -= history[writeIndex];
		history[writeIndex] = value;
		movingSum += value;

		writeIndex = (writeIndex + 1) % HISTORY_LENGTH;

		// 起動直後は履歴がまだ埋まっていないので、分母を実際に積んだ数へ絞る。フルの長さで割ると
		// 埋まっていない分の 0 に引きずられて、浅い平均が実際より甘い（短い）値になってしまう。
		if (sampleCount < HISTORY_LENGTH)
		{
			++sampleCount;
		}
	}


	float RenderStatisticsPanel::MovingAverage::Get() const
	{
		return sampleCount > 0 ? movingSum / static_cast<float>(sampleCount) : 0.0f;
	}


	void RenderStatisticsPanel::MovingAverage::Reset()
	{
		for (float& sample : history)
		{
			sample = 0.0f;
		}

		writeIndex  = 0;
		sampleCount = 0;
		movingSum   = 0.0f;
	}


	bool RenderStatisticsPanel::Initialize()
	{
		ResetAverages();

		return true;
	}


	void RenderStatisticsPanel::Shutdown()
	{
		ResetAverages();
	}


	void RenderStatisticsPanel::ResetAverages()
	{
		m_frameTimeSeconds.Reset();

#if FANG_ENABLE_PROFILER
		m_recordMilliseconds.Reset();
		m_presentMilliseconds.Reset();
		m_gpuWaitMilliseconds.Reset();
		m_gpuFrameMilliseconds.Reset();

		for (MovingAverage& passGpuMilliseconds : m_passGpuMilliseconds)
		{
			passGpuMilliseconds.Reset();
		}

		std::memset(m_passNames, 0, sizeof(m_passNames));
#endif
	}


	void RenderStatisticsPanel::BuildFrame(float deltaTimeSeconds, const RenderStatistics& renderStatistics)
	{
		// 窓が閉じていても積む。開いた瞬間に空の平均を見せないため。
		m_frameTimeSeconds.Push(deltaTimeSeconds);

#if FANG_ENABLE_PROFILER
		PushTimingSamples(renderStatistics);
#endif

		ApplyPanelPlacement(EnPanelSlot::RenderStatistics);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin("レンダリング統計", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		BuildFrameTimeSection();
		ImGui::Separator();
		BuildRenderStatisticsSection(renderStatistics);

#if FANG_ENABLE_PROFILER
		ImGui::Separator();
		BuildRenderTimeBreakdownSection();
		ImGui::Separator();
		BuildGpuTimeSection(renderStatistics);
#endif

		ImGui::End();
	}


	void RenderStatisticsPanel::BuildFrameTimeSection() const
	{
		const float averageFrameTimeSeconds      = m_frameTimeSeconds.Get();
		const float averageFrameTimeMilliseconds = averageFrameTimeSeconds * 1000.0f;
		const float averageFramesPerSecond = averageFrameTimeSeconds > 0.0f ? 1.0f / averageFrameTimeSeconds : 0.0f;

		ImGui::Text("フレーム時間: %.2f ms（移動平均 %u フレーム）", averageFrameTimeMilliseconds, HISTORY_LENGTH);
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


#if FANG_ENABLE_PROFILER

	void RenderStatisticsPanel::PushTimingSamples(const RenderStatistics& renderStatistics)
	{
		m_recordMilliseconds.Push(renderStatistics.recordMilliseconds);
		m_presentMilliseconds.Push(renderStatistics.presentMilliseconds);
		m_gpuWaitMilliseconds.Push(renderStatistics.gpuWaitMilliseconds);

		if (!renderStatistics.hasGpuTimestamps)
		{
			return;
		}

		m_gpuFrameMilliseconds.Push(renderStatistics.gpuFrameMilliseconds);

		const uint32_t passCount = std::min(renderStatistics.timedPassCount, RenderStatistics::MAX_TIMED_PASS_COUNT);

		for (uint32_t passIndex = 0; passIndex < RenderStatistics::MAX_TIMED_PASS_COUNT; ++passIndex)
		{
			// パスの出入り（キャスタの無いフレームの Shadow など）で番号がずれると、別のパスの値が混ざる。
			// 名前が変わった番号は履歴を捨ててから積み直す。無くなった番号は名前を空にして、次に来たら新規扱い。
			const char* name = passIndex < passCount ? renderStatistics.passGpuTimes[passIndex].name : "";
			if (std::strncmp(m_passNames[passIndex], name, RenderPassGpuTime::MAX_NAME_LENGTH) != 0)
			{
				std::memset(m_passNames[passIndex], 0, RenderPassGpuTime::MAX_NAME_LENGTH);
				std::memcpy(m_passNames[passIndex], name, std::strlen(name));

				m_passGpuMilliseconds[passIndex].Reset();
			}

			if (passIndex < passCount)
			{
				m_passGpuMilliseconds[passIndex].Push(renderStatistics.passGpuTimes[passIndex].milliseconds);
			}
		}
	}


	void RenderStatisticsPanel::BuildRenderTimeBreakdownSection() const
	{
		// 3 つの和が「エンジン情報」の描画 ms とほぼ一致する。どれが大きいかで手を入れる場所が決まる。
		ImGui::Text("描画（メイン）の内訳（移動平均）");
		ImGui::Text("  記録: %.2f ms", m_recordMilliseconds.Get());
		ImGui::Text("  Present: %.2f ms", m_presentMilliseconds.Get());
		ImGui::Text("  GPU 完了待ち: %.2f ms", m_gpuWaitMilliseconds.Get());
	}


	void RenderStatisticsPanel::BuildGpuTimeSection(const RenderStatistics& renderStatistics) const
	{
		if (!renderStatistics.hasGpuTimestamps)
		{
			ImGui::Text("GPU 時間: なし");
			return;
		}

		// 合計は先頭パスの開始から末尾パスの終了までの区間。パスの和より大きければ、その差が本と本の隙間。
		ImGui::Text("GPU 合計: %.2f ms", m_gpuFrameMilliseconds.Get());

		const uint32_t passCount = std::min(renderStatistics.timedPassCount, RenderStatistics::MAX_TIMED_PASS_COUNT);

		for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
		{
			ImGui::Text("  %s", renderStatistics.passGpuTimes[passIndex].name);
			ImGui::SameLine(PASS_TIME_COLUMN_OFFSET);
			ImGui::Text("%.2f ms", m_passGpuMilliseconds[passIndex].Get());
		}
	}

#endif
} // namespace fang::editor
