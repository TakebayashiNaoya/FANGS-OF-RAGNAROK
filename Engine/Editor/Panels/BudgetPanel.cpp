/**
 * @file BudgetPanel.cpp
 * @brief Xbox 予算パネルの中身。メモリの使用量と、フレーム時間の Xbox 換算。
 */
#include "Pch.h"
#include "Editor/Panels/BudgetPanel.h"
#include "Core/Platform/Budget.h"
#include "Core/Platform/Thread.h"
#include "Editor/EditorLayout.h"
#include "Runtime/EngineContext.h"
#include "Runtime/FrameClock.h"
#include <imgui.h>


namespace fang::editor
{
	namespace
	{
		/** @brief 「Xbox 予算」ウィンドウの幅。 */
		constexpr float WINDOW_MIN_WIDTH = 400.0f;

		/** @brief 使用量のバーの幅。 */
		constexpr float PROGRESS_BAR_WIDTH = 260.0f;

		/** @brief 予算を超えているときの色。 */
		constexpr ImVec4 OVER_BUDGET_COLOR{ 1.0f, 0.4f, 0.35f, 1.0f };

		/** @brief 予算に近いときの色。 */
		constexpr ImVec4 NEAR_BUDGET_COLOR{ 1.0f, 0.8f, 0.3f, 1.0f };


		/** @brief バイトを MiB に直す。表示のためだけの変換。 */
		[[nodiscard]] float ToMebibytes(uint64_t bytes)
		{
			return static_cast<float>(static_cast<double>(bytes) / (1024.0 * 1024.0));
		}


		/**
		 * @brief 割合に応じた色を返す。
		 * @param ratio 0.0 が空、1.0 で予算ちょうど。
		 */
		[[nodiscard]] ImVec4 PickRatioColor(float ratio)
		{
			if (ratio >= 1.0f)
			{
				return OVER_BUDGET_COLOR;
			}

			if (ratio >= budget::MEMORY_WARNING_RATIO)
			{
				return NEAR_BUDGET_COLOR;
			}

			return ImGui::GetStyleColorVec4(ImGuiCol_Text);
		}
	} // namespace


	bool BudgetPanel::Initialize(const EngineContext& context)
	{
		m_budget     = &context.platformBudget;
		m_frameClock = &context.frameClock;
		return true;
	}


	void BudgetPanel::Shutdown()
	{
		m_budget     = nullptr;
		m_frameClock = nullptr;
	}


	void BudgetPanel::BuildFrame()
	{
		if (m_budget == nullptr)
		{
			return;
		}

		ApplyPanelPlacement(EnPanelSlot::Budget);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin("Xbox 予算", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		BuildMemorySection();
		ImGui::Separator();
		BuildCpuSection();

		ImGui::End();
	}


	void BudgetPanel::BuildMemorySection() const
	{
		ImGui::SeparatorText("メモリ");

		const uint64_t usedBytes = m_budget->GetMemoryUsedBytes();
		if (usedBytes == 0)
		{
			ImGui::TextDisabled("まだ測れていない");
			return;
		}

		const float limitMebibytes = ToMebibytes(budget::MEMORY_LIMIT_BYTES);
		const float usedRatio      = ToMebibytes(usedBytes) / limitMebibytes;

		ImGui::TextColored(
			PickRatioColor(usedRatio),
			"使用量: %.1f MiB / %.0f MiB（%.1f%%）",
			ToMebibytes(usedBytes),
			limitMebibytes,
			usedRatio * 100.0f
		);

		ImGui::ProgressBar(usedRatio, ImVec2(PROGRESS_BAR_WIDTH, 0.0f));
		ImGui::Text("最高水位: %.1f MiB", ToMebibytes(m_budget->GetMemoryPeakBytes()));

		// OS 申告の上限は実機かどうかの目安になる。5GB 前後なら Game 分類で動いている。
		const uint64_t systemLimitBytes = m_budget->GetSystemMemoryLimitBytes();
		if (systemLimitBytes == 0)
		{
			ImGui::TextDisabled("OS 申告の上限: 無し（PC。判定は上の 5GB で行う）");
		}
		else
		{
			ImGui::Text("OS 申告の上限: %.0f MiB", ToMebibytes(systemLimitBytes));
		}
	}


	void BudgetPanel::BuildCpuSection()
	{
		ImGui::SeparatorText("CPU");

		const float budgetMilliseconds = budget::FRAME_BUDGET_SECONDS * 1000.0f;
		const float workMilliseconds   = m_budget->GetFrameWorkSeconds() * 1000.0f;
		const float scaledMilliseconds = m_budget->GetScaledFrameSeconds() * 1000.0f;
		const float scaledRatio        = scaledMilliseconds / budgetMilliseconds;

		// Windows ビルドは実コア数で走るので、予算の値だけ出すと実態とずれる。両方見せる。
		const uint32_t usableCoreCount = GetUsableCoreCount();
		if (usableCoreCount == budget::USABLE_CORE_COUNT)
		{
			ImGui::Text("使えるコア: %u（Xbox の割り当てどおり。占有 4 + 共有 2）", usableCoreCount);
		}
		else
		{
			ImGui::TextColored(
				NEAR_BUDGET_COLOR,
				"使えるコア: %u（Xbox の割り当ては %u）",
				usableCoreCount,
				budget::USABLE_CORE_COUNT
			);
		}
		ImGui::Text("実処理: %.2f ms", workMilliseconds);

		ImGui::TextColored(
			PickRatioColor(scaledRatio),
			"Xbox 換算: %.2f ms / 予算 %.2f ms（%.0f%%）",
			scaledMilliseconds,
			budgetMilliseconds,
			scaledRatio * 100.0f
		);

		ImGui::ProgressBar(scaledRatio, ImVec2(PROGRESS_BAR_WIDTH, 0.0f));

		float scaleFactor = m_budget->GetCpuScaleFactor();
		// 幅を決めておかないとラベルがウィンドウの右端で切れる。
		ImGui::SetNextItemWidth(PROGRESS_BAR_WIDTH);
		if (ImGui::SliderFloat(
				"倍率",
				&scaleFactor,
				budget::MINIMUM_CPU_SCALE_FACTOR,
				budget::MAXIMUM_CPU_SCALE_FACTOR,
				"%.2f 倍"
			))
		{
			m_budget->SetCpuScaleFactor(scaleFactor);
		}

		bool isThrottleEnabled = m_budget->IsThrottleEnabled();
		if (ImGui::Checkbox("Xbox 換算の時間まで待たせる", &isThrottleEnabled))
		{
			m_budget->SetThrottleEnabled(isThrottleEnabled);
		}

		if (m_frameClock != nullptr)
		{
			ImGui::Text(
				"上限: %.1f ms（1/25 秒）／ 切られた周: %u 回",
				MAXIMUM_DELTA_TIME_SECONDS * 1000.0f,
				m_frameClock->GetClampedFrameCount()
			);
		}

		if (scaleFactor <= budget::MINIMUM_CPU_SCALE_FACTOR)
		{
			// 状態は短い 1 行に分けておく。1 行にまとめると「1.00 / 倍」の位置で折り返して読みにくい。
			ImGui::TextDisabled("倍率 1.00 倍。換算していない（実機ではこれでよい）。");
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped(
				"PC で換算したいときは、テスト負荷を実機と PC で回して直列の所要時間の比をここへ入れる。"
			);
			ImGui::PopStyleColor();
		}
		else
		{
			// 換算で分かるのは総量だけ。どこが遅いかまでは出ないので、そう読ませる。
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("換算で分かるのは総量だけ。どこが遅いかは実機で測ること。");
			ImGui::PopStyleColor();
		}
	}
} // namespace fang::editor
