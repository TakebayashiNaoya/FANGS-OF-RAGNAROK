/**
 * @file JobSystemPanel.cpp
 * @brief ジョブシステムのパネルの中身。実行者の表、プールの使用量、押している間だけかける負荷。
 */
#include "Pch.h"
#include "Editor/Panels/JobSystemPanel.h"
#include "Core/Job/ParallelFor.h"
#include "Runtime/EngineContext.h"
#include <imgui.h>


namespace fang::editor
{
	namespace
	{
		/** @brief 「ジョブシステム」ウィンドウの最小幅。自動サイズだけだと日本語ラベルの末尾が切れる。 */
		constexpr float WINDOW_MIN_WIDTH = 420.0f;

		/** @brief 初回に置く位置。既定のままだとエンジン情報ウィンドウの真上に重なる。 */
		constexpr ImVec2 FIRST_USE_POSITION{ 460.0f, 60.0f };

		/** @brief 毎秒の実行数を出し直す間隔。1 フレームごとに割ると数字が跳ねて読めない。 */
		constexpr float SAMPLE_INTERVAL_SECONDS = 0.5f;

		/** @brief プールの使用量を出すバーの幅。隣にリセットボタンを並べるので幅を決め打つ。 */
		constexpr float POOL_PROGRESS_BAR_WIDTH = 220.0f;

		/** @brief 負荷が足し合わせる要素数。60fps を割らない範囲で、表の数字が動くだけ積む。 */
		constexpr uint32_t TEST_LOAD_ELEMENT_COUNT = 65536;

		/** @brief 負荷のジョブ 1 件が受け持つ要素数。 */
		constexpr uint32_t TEST_LOAD_BATCH_SIZE = 256;

		/** @brief 0 から始まる連番の総和。画面に出すので、この検算は最適化で消えない。 */
		constexpr uint64_t EXPECTED_TEST_LOAD_SUM =
			static_cast<uint64_t>(TEST_LOAD_ELEMENT_COUNT) * (TEST_LOAD_ELEMENT_COUNT - 1) / 2;

		/** @brief 見逃してほしくない数字の色。 */
		constexpr ImVec4 WARNING_COLOR{ 1.0f, 0.45f, 0.4f, 1.0f };
	} // namespace

	bool JobSystemPanel::Initialize(const EngineContext& context)
	{
		m_jobSystem = &context.jobSystem;

		m_testLoadValues.resize(TEST_LOAD_ELEMENT_COUNT);
		for (uint32_t i = 0; i < TEST_LOAD_ELEMENT_COUNT; ++i)
		{
			m_testLoadValues[i] = i;
		}

		return true;
	}

	void JobSystemPanel::Shutdown()
	{
		m_testLoadValues.clear();
		m_testLoadValues.shrink_to_fit();

		m_jobSystem = nullptr;
	}

	void JobSystemPanel::BuildFrame(float deltaTimeSeconds)
	{
		FANG_ASSERT(m_jobSystem != nullptr, "JobSystemPanel が初期化されていない");

		ImGui::SetNextWindowPos(FIRST_USE_POSITION, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin("ジョブシステム", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		const uint32_t workerCount = m_jobSystem->GetWorkerCount();
		ImGui::Text("ワーカー数: %u", workerCount);
		ImGui::Text("実行参加数: %u (メイン 1 + ワーカー %u)", m_jobSystem->GetExecutorCount(), workerCount);
		ImGui::Text("生きているスレッド: %u", m_jobSystem->GetRunningThreadCount());
		ImGui::Separator();

#if FANG_ENABLE_PROFILER
		UpdateExecutedJobCountsPerSecond(deltaTimeSeconds);
		BuildExecutorTable();
		ImGui::Separator();
		BuildJobPoolSection();
#else
		FANG_UNUSED(deltaTimeSeconds);
		ImGui::TextUnformatted("この構成は統計を採っていないので、実行数とプールの使用量は出ない。");
#endif

		ImGui::Separator();
		BuildTestLoadSection();

		ImGui::End();
	}

	/***************************************************************************************************/

#if FANG_ENABLE_PROFILER

	void JobSystemPanel::UpdateExecutedJobCountsPerSecond(float deltaTimeSeconds)
	{
		m_elapsedSecondsInInterval += deltaTimeSeconds;
		if (m_elapsedSecondsInInterval < SAMPLE_INTERVAL_SECONDS)
		{
			return;
		}

		const uint32_t executorCount = m_jobSystem->GetExecutorCount();
		for (uint32_t workerIndex = 0; workerIndex < executorCount; ++workerIndex)
		{
			const uint64_t executedJobCount = m_jobSystem->GetExecutedJobCount(workerIndex);
			const uint64_t countInInterval  = executedJobCount - m_previousExecutedJobCounts[workerIndex];

			m_previousExecutedJobCounts[workerIndex] = executedJobCount;
			m_executedJobCountsPerSecond[workerIndex] =
				static_cast<float>(countInInterval) / m_elapsedSecondsInInterval;
		}

		m_elapsedSecondsInInterval = 0.0f;
	}

	void JobSystemPanel::BuildExecutorTable()
	{
		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

		if (!ImGui::BeginTable("実行者", 4, TABLE_FLAGS))
		{
			return;
		}

		ImGui::TableSetupColumn("番号");
		ImGui::TableSetupColumn("実行者");
		ImGui::TableSetupColumn("累計");
		ImGui::TableSetupColumn("毎秒");
		ImGui::TableHeadersRow();

		uint64_t totalExecutedJobCount          = 0;
		float    totalExecutedJobCountPerSecond = 0.0f;

		const uint32_t executorCount = m_jobSystem->GetExecutorCount();
		for (uint32_t workerIndex = 0; workerIndex < executorCount; ++workerIndex)
		{
			const uint64_t executedJobCount          = m_jobSystem->GetExecutedJobCount(workerIndex);
			const float    executedJobCountPerSecond = m_executedJobCountsPerSecond[workerIndex];

			totalExecutedJobCount += executedJobCount;
			totalExecutedJobCountPerSecond += executedJobCountPerSecond;

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			ImGui::Text("%u", workerIndex);

			ImGui::TableNextColumn();
			if (workerIndex == JobSystem::MAIN_WORKER_INDEX)
			{
				ImGui::TextUnformatted("メイン");
			}
			else
			{
				ImGui::Text("ワーカー %u", workerIndex);
			}

			ImGui::TableNextColumn();
			ImGui::Text("%llu", executedJobCount);

			ImGui::TableNextColumn();
			ImGui::Text("%.0f", executedJobCountPerSecond);
		}

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("合計");
		ImGui::TableNextColumn();
		ImGui::Text("%llu", totalExecutedJobCount);
		ImGui::TableNextColumn();
		ImGui::Text("%.0f", totalExecutedJobCountPerSecond);

		ImGui::EndTable();

		const uint64_t inlineExecutedJobCount = m_jobSystem->GetInlineExecutedJobCount();
		if (inlineExecutedJobCount == 0)
		{
			ImGui::Text("その場実行への縮退: %llu 件", inlineExecutedJobCount);
		}
		else
		{
			ImGui::TextColored(WARNING_COLOR, "その場実行への縮退: %llu 件", inlineExecutedJobCount);
		}
	}

	void JobSystemPanel::BuildJobPoolSection()
	{
		const uint32_t jobsInUseCount     = m_jobSystem->GetJobsInUseCount();
		const uint32_t peakJobsInUseCount = m_jobSystem->GetPeakJobsInUseCount();

		ImGui::Text(
			"ジョブプール: 使用中 %u / 高水位 %u / 上限 %u",
			jobsInUseCount,
			peakJobsInUseCount,
			JobSystem::JOB_POOL_CAPACITY
		);

		const float peakRatio =
			static_cast<float>(peakJobsInUseCount) / static_cast<float>(JobSystem::JOB_POOL_CAPACITY);
		ImGui::ProgressBar(peakRatio, ImVec2(POOL_PROGRESS_BAR_WIDTH, 0.0f));

		ImGui::SameLine();
		if (ImGui::Button("高水位をリセット"))
		{
			m_jobSystem->ResetPeakJobsInUseCount();
		}
	}

#endif

	void JobSystemPanel::BuildTestLoadSection()
	{
		// 押している間だけ真になる。押した瞬間だけでは表の数字が動かない。
		ImGui::Button("テスト負荷（押している間）");
		if (ImGui::IsItemActive())
		{
			RunTestLoad();
		}

		ImGui::Text(
			"要素数 %u / 分割幅 %u / ジョブ %u 件",
			TEST_LOAD_ELEMENT_COUNT,
			TEST_LOAD_BATCH_SIZE,
			TEST_LOAD_ELEMENT_COUNT / TEST_LOAD_BATCH_SIZE
		);

		if (!m_hasRunTestLoad)
		{
			ImGui::TextUnformatted("総和: まだ回していない");
			return;
		}

		if (m_isTestLoadCorrect)
		{
			ImGui::Text("総和: %llu（一致）", m_testLoadSum);
		}
		else
		{
			ImGui::TextColored(
				WARNING_COLOR,
				"総和: %llu（不一致。期待値 %llu）",
				m_testLoadSum,
				EXPECTED_TEST_LOAD_SUM
			);
		}
	}

	void JobSystemPanel::RunTestLoad()
	{
		for (uint64_t& partialSum : m_testLoadPartialSums)
		{
			partialSum = 0;
		}

		const uint32_t* const values      = m_testLoadValues.data();
		uint64_t* const       partialSums = m_testLoadPartialSums;

		// 同じワーカー番号を名乗るスレッドは 1 本だけなので、部分和の加算に同期は要らない。
		ParallelFor(
			*m_jobSystem,
			0,
			TEST_LOAD_ELEMENT_COUNT,
			TEST_LOAD_BATCH_SIZE,
			[values, partialSums](uint32_t index, uint32_t workerIndex) { partialSums[workerIndex] += values[index]; }
		);

		uint64_t totalSum = 0;
		for (const uint64_t partialSum : m_testLoadPartialSums)
		{
			totalSum += partialSum;
		}

		m_testLoadSum       = totalSum;
		m_isTestLoadCorrect = (totalSum == EXPECTED_TEST_LOAD_SUM);
		m_hasRunTestLoad    = true;
	}
} // namespace fang::editor
