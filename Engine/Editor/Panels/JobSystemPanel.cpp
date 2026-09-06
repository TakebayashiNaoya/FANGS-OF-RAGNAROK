/**
 * @file JobSystemPanel.cpp
 * @brief ジョブシステムのパネルの中身。実行者の表、プールの使用量、並列と直列を並べて測る負荷。
 */
#include "Pch.h"
#include "Editor/Panels/JobSystemPanel.h"
#include "Core/Job/ParallelFor.h"
#include "Editor/EditorLayout.h"
#include "Runtime/EngineContext.h"
#include <imgui.h>
#include <chrono>


namespace fang::editor
{
	namespace
	{
		/** @brief 「ジョブシステム」ウィンドウの最小幅。自動サイズだけだと日本語ラベルの末尾が切れる。 */
		constexpr float WINDOW_MIN_WIDTH = 420.0f;

		/** @brief 毎秒の実行数を出し直す間隔。1 フレームごとに割ると数字が跳ねて読めない。 */
		constexpr float SAMPLE_INTERVAL_SECONDS = 0.5f;

		/** @brief プールの使用量を出すバーの幅。隣にリセットボタンを並べるので幅を決め打つ。 */
		constexpr float POOL_PROGRESS_BAR_WIDTH = 220.0f;

		/** @brief 負荷の条件を動かすスライダの幅。ラベルまで含めて窓の幅に収める。 */
		constexpr float TEST_LOAD_SLIDER_WIDTH = 200.0f;

		/** @brief 負荷が足し合わせる要素数の上限。Initialize はこの数で 1 回だけ確保する。 */
		constexpr uint32_t MAX_TEST_LOAD_ELEMENT_COUNT = 1u << 20;

		/** @brief 要素数の下限。これより減らすと所要時間が計測の粒度に埋もれる。 */
		constexpr uint32_t MIN_TEST_LOAD_ELEMENT_COUNT = 1024;

		/** @brief 起動時の要素数。60fps を割らない範囲で、表の数字が動くだけ積む。 */
		constexpr uint32_t DEFAULT_TEST_LOAD_ELEMENT_COUNT = 65536;

		/** @brief 分割幅の下限。 */
		constexpr uint32_t MIN_TEST_LOAD_BATCH_SIZE = 16;

		/** @brief 分割幅の上限。ここまで広げるとジョブが 1 件になり、並列版が直列版に近づく。 */
		constexpr uint32_t MAX_TEST_LOAD_BATCH_SIZE = 65536;

		/** @brief 起動時の分割幅。 */
		constexpr uint32_t DEFAULT_TEST_LOAD_BATCH_SIZE = 256;

		/**
		 * @brief 負荷が積んでよいジョブの件数。
		 * @details プールを使い切るとその場実行へ縮退し、並列版の所要時間が直列版に化けて比較にならない。
		 *          プールはこの負荷だけのものではないので、半分までに抑える。
		 */
		constexpr uint32_t MAX_TEST_LOAD_JOB_COUNT = JobSystem::JOB_POOL_CAPACITY / 2;

		/** @brief 見逃してほしくない数字の色。 */
		constexpr ImVec4 WARNING_COLOR{ 1.0f, 0.45f, 0.4f, 1.0f };

		/** @brief 控えておいた時点から今までの時間をミリ秒で返す。 */
		float GetElapsedMilliseconds(const std::chrono::steady_clock::time_point& beginTime)
		{
			const auto endTime = std::chrono::steady_clock::now();
			return std::chrono::duration<float, std::milli>(endTime - beginTime).count();
		}

		/** @brief 0 から始まる連番 elementCount 個の総和。画面に出すので、この検算は最適化で消えない。 */
		uint64_t GetExpectedTestLoadSum(uint32_t elementCount)
		{
			return static_cast<uint64_t>(elementCount) * (elementCount - 1) / 2;
		}

		/** @brief 要素数と分割幅から出るジョブの件数。最後の 1 件が端数を受け持つ。 */
		uint32_t GetTestLoadJobCount(uint32_t elementCount, uint32_t batchSize)
		{
			return (elementCount + batchSize - 1) / batchSize;
		}

		/** @brief ジョブの件数を MAX_TEST_LOAD_JOB_COUNT に収めるのに要る分割幅の下限。 */
		uint32_t GetMinimumTestLoadBatchSize(uint32_t elementCount)
		{
			const uint32_t batchSize = (elementCount + MAX_TEST_LOAD_JOB_COUNT - 1) / MAX_TEST_LOAD_JOB_COUNT;
			return batchSize > MIN_TEST_LOAD_BATCH_SIZE ? batchSize : MIN_TEST_LOAD_BATCH_SIZE;
		}
	} // namespace


	void JobSystemPanel::MillisecondsAverage::Push(float milliseconds)
	{
		movingSumMilliseconds -= history[writeIndex];
		history[writeIndex] = milliseconds;
		movingSumMilliseconds += milliseconds;

		writeIndex = (writeIndex + 1) % TEST_LOAD_HISTORY_LENGTH;
		if (sampleCount < TEST_LOAD_HISTORY_LENGTH)
		{
			++sampleCount;
		}
	}


	float JobSystemPanel::MillisecondsAverage::GetAverage() const
	{
		if (sampleCount == 0)
		{
			return 0.0f;
		}

		return movingSumMilliseconds / static_cast<float>(sampleCount);
	}


	void JobSystemPanel::MillisecondsAverage::Reset()
	{
		for (float& sample : history)
		{
			sample = 0.0f;
		}

		writeIndex            = 0;
		sampleCount           = 0;
		movingSumMilliseconds = 0.0f;
	}


	bool JobSystemPanel::Initialize(const EngineContext& context)
	{
		m_jobSystem = &context.jobSystem;

		m_testLoadValues.resize(MAX_TEST_LOAD_ELEMENT_COUNT);
		for (uint32_t i = 0; i < MAX_TEST_LOAD_ELEMENT_COUNT; ++i)
		{
			m_testLoadValues[i] = i;
		}

		m_testLoadElementCount = DEFAULT_TEST_LOAD_ELEMENT_COUNT;
		m_testLoadBatchSize    = DEFAULT_TEST_LOAD_BATCH_SIZE;

		return true;
	}


	void JobSystemPanel::Shutdown()
	{
		m_testLoadValues.clear();
		m_testLoadValues.shrink_to_fit();

		m_parallelAverage.Reset();
		m_serialAverage.Reset();

		m_jobSystem = nullptr;
	}


	void JobSystemPanel::BuildFrame(float deltaTimeSeconds, uint64_t updatingFrameIndex)
	{
		FANG_ASSERT(m_jobSystem != nullptr, "JobSystemPanel が初期化されていない");

		ApplyPanelPlacement(EnPanelSlot::JobSystem);
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

		// 走っている更新（フレーム N）が触るのは N の面なので、描画側はその反対だけを触る。
		BuildTestLoadSection(updatingFrameIndex + 1);

		ImGui::End();
	}


	void JobSystemPanel::RunRequestedTestLoad(uint64_t frameIndex)
	{
		FANG_ASSERT(m_jobSystem != nullptr, "JobSystemPanel が初期化されていない");

		TestLoadSlot& slot = m_testLoadSlots[GetSlotIndex(frameIndex)];
		if (!slot.isRequested)
		{
			return;
		}

		RunTestLoad(slot);

		slot.runFrameIndex = frameIndex;
		slot.hasRun        = true;
	}


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


	void JobSystemPanel::BuildTestLoadSection(uint64_t drawingFrameIndex)
	{
		TestLoadSlot& slot = m_testLoadSlots[GetSlotIndex(drawingFrameIndex)];

		BuildTestLoadParameters();

		// 面の条件を書き換える前に、前の周の結果を引き取る。
		PushTestLoadSamples(slot);

		slot.isRequested  = m_isTestLoadRunning;
		slot.elementCount = m_testLoadElementCount;
		slot.batchSize    = m_testLoadBatchSize;

		// 先に回したほうはキャッシュが冷えているぶん損をする。
		// 4 フレーム周期で順を入れ替え、どちらの面にも両方の順が回るようにする。
		slot.isSerialFirst = (drawingFrameIndex % 4) >= 2;

		if (m_parallelAverage.sampleCount == 0)
		{
			BuildTestLoadWaitingText();
			return;
		}

		BuildTestLoadComparisonTable();
	}


	void JobSystemPanel::BuildTestLoadParameters()
	{
		// 入れている間ずっと回る。押している間だけのボタンにすると、回しながらスライダを動かせない。
		ImGui::Checkbox("テスト負荷を回す", &m_isTestLoadRunning);

		bool isChanged = false;

		int elementCount = static_cast<int>(m_testLoadElementCount);
		ImGui::SetNextItemWidth(TEST_LOAD_SLIDER_WIDTH);
		if (ImGui::SliderInt(
				"要素数",
				&elementCount,
				static_cast<int>(MIN_TEST_LOAD_ELEMENT_COUNT),
				static_cast<int>(MAX_TEST_LOAD_ELEMENT_COUNT),
				"%d",
				ImGuiSliderFlags_Logarithmic
			))
		{
			m_testLoadElementCount = static_cast<uint32_t>(elementCount);
			isChanged              = true;
		}

		int batchSize = static_cast<int>(m_testLoadBatchSize);
		ImGui::SetNextItemWidth(TEST_LOAD_SLIDER_WIDTH);
		if (ImGui::SliderInt(
				"分割幅",
				&batchSize,
				static_cast<int>(MIN_TEST_LOAD_BATCH_SIZE),
				static_cast<int>(MAX_TEST_LOAD_BATCH_SIZE),
				"%d",
				ImGuiSliderFlags_Logarithmic
			))
		{
			m_testLoadBatchSize = static_cast<uint32_t>(batchSize);
			isChanged           = true;
		}

		// 要素数を増やすと、同じ分割幅でもジョブが増えてプールを食い尽くす。下限のほうを押し上げる。
		const uint32_t minimumBatchSize = GetMinimumTestLoadBatchSize(m_testLoadElementCount);
		if (m_testLoadBatchSize < minimumBatchSize)
		{
			m_testLoadBatchSize = minimumBatchSize;
			isChanged           = true;
		}

		if (isChanged)
		{
			m_parallelAverage.Reset();
			m_serialAverage.Reset();
		}

		ImGui::Text(
			"ジョブ %u 件（分割幅の下限 %u）",
			GetTestLoadJobCount(m_testLoadElementCount, m_testLoadBatchSize),
			minimumBatchSize
		);
	}


	void JobSystemPanel::BuildTestLoadWaitingText() const
	{
		if (!m_isTestLoadRunning)
		{
			ImGui::TextUnformatted("止まっている。上のチェックを入れると回り始める。");
			return;
		}

		ImGui::TextUnformatted("条件が変わったので取り直している。数フレームで出る。");
	}


	void JobSystemPanel::PushTestLoadSamples(const TestLoadSlot& slot)
	{
		if (!slot.hasRun || slot.runFrameIndex == m_lastAveragedFrameIndex)
		{
			return;
		}

		// 条件を変えた直後は、まだ前の条件で回した面が残っている。混ぜない。
		if (slot.elementCount != m_testLoadElementCount || slot.batchSize != m_testLoadBatchSize)
		{
			return;
		}

		m_parallelAverage.Push(slot.parallel.milliseconds);
		m_serialAverage.Push(slot.serial.milliseconds);

		m_lastParallelResult     = slot.parallel;
		m_lastSerialResult       = slot.serial;
		m_lastAveragedFrameIndex = slot.runFrameIndex;
	}


	void JobSystemPanel::BuildTestLoadComparisonTable() const
	{
		constexpr ImGuiTableFlags TABLE_FLAGS =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

		if (!ImGui::BeginTable("負荷の比較", 4, TABLE_FLAGS))
		{
			return;
		}

		ImGui::TableSetupColumn("回し方");
		ImGui::TableSetupColumn("直近");
		ImGui::TableSetupColumn("移動平均");
		ImGui::TableSetupColumn("総和");
		ImGui::TableHeadersRow();

		BuildTestLoadRow("並列 (ParallelFor)", m_lastParallelResult, m_parallelAverage.GetAverage());
		BuildTestLoadRow("直列 (SerialFor)", m_lastSerialResult, m_serialAverage.GetAverage());

		ImGui::EndTable();

		const float parallelMilliseconds = m_parallelAverage.GetAverage();
		const float serialMilliseconds   = m_serialAverage.GetAverage();
		if (parallelMilliseconds > 0.0f)
		{
			ImGui::Text("倍率: %.2f 倍（直列 ÷ 並列）", serialMilliseconds / parallelMilliseconds);
			ImGui::TextUnformatted("1 を割ったら、積み下ろしのほうが並列で稼いだぶんより高くついている。");
		}

		ImGui::Text(
			"移動平均のサンプル: %u / %u。条件を変えると空に戻る。",
			m_parallelAverage.sampleCount,
			TEST_LOAD_HISTORY_LENGTH
		);
	}


	void JobSystemPanel::BuildTestLoadRow(
		const char*           label,
		const TestLoadResult& result,
		float                 averageMilliseconds
	) const
	{
		ImGui::TableNextRow();

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(label);

		ImGui::TableNextColumn();
		ImGui::Text("%.3f ms", result.milliseconds);

		ImGui::TableNextColumn();
		ImGui::Text("%.3f ms", averageMilliseconds);

		ImGui::TableNextColumn();
		if (result.isCorrect)
		{
			ImGui::Text("%llu（一致）", result.sum);
		}
		else
		{
			ImGui::TextColored(WARNING_COLOR, "%llu（不一致）", result.sum);
		}
	}


	void JobSystemPanel::RunTestLoad(TestLoadSlot& slot)
	{
		// 先に回したほうはキャッシュが冷えているぶん損をする。呼ぶ側が周期で順を入れ替えて均す。
		if (slot.isSerialFirst)
		{
			slot.serial   = RunSerialTestLoad(slot.elementCount);
			slot.parallel = RunParallelTestLoad(slot.elementCount, slot.batchSize);
		}
		else
		{
			slot.parallel = RunParallelTestLoad(slot.elementCount, slot.batchSize);
			slot.serial   = RunSerialTestLoad(slot.elementCount);
		}
	}


	JobSystemPanel::TestLoadResult JobSystemPanel::RunParallelTestLoad(uint32_t elementCount, uint32_t batchSize)
	{
		ClearTestLoadPartialSums();

		const uint32_t* const values      = m_testLoadValues.data();
		uint64_t* const       partialSums = m_testLoadPartialSums;

		const auto beginTime = std::chrono::steady_clock::now();

		// 同じワーカー番号を名乗るスレッドは 1 本だけなので、部分和の加算に同期は要らない。
		ParallelFor(
			*m_jobSystem,
			0,
			elementCount,
			batchSize,
			[values, partialSums](uint32_t index, uint32_t workerIndex) { partialSums[workerIndex] += values[index]; }
		);

		return MakeTestLoadResult(GetElapsedMilliseconds(beginTime), elementCount);
	}


	JobSystemPanel::TestLoadResult JobSystemPanel::RunSerialTestLoad(uint32_t elementCount)
	{
		ClearTestLoadPartialSums();

		const uint32_t* const values      = m_testLoadValues.data();
		uint64_t* const       partialSums = m_testLoadPartialSums;

		const auto beginTime = std::chrono::steady_clock::now();

		// 走るのはこのスレッド 1 本だけなので、部分和はどの枠へ足してもよい。
		// 並列版とまったく同じ本体を渡すために、番号だけメインを名乗る。
		SerialFor(
			0,
			elementCount,
			JobSystem::MAIN_WORKER_INDEX,
			[values, partialSums](uint32_t index, uint32_t workerIndex) { partialSums[workerIndex] += values[index]; }
		);

		return MakeTestLoadResult(GetElapsedMilliseconds(beginTime), elementCount);
	}


	void JobSystemPanel::ClearTestLoadPartialSums()
	{
		for (uint64_t& partialSum : m_testLoadPartialSums)
		{
			partialSum = 0;
		}
	}


	JobSystemPanel::TestLoadResult JobSystemPanel::MakeTestLoadResult(float milliseconds, uint32_t elementCount) const
	{
		uint64_t totalSum = 0;
		for (const uint64_t partialSum : m_testLoadPartialSums)
		{
			totalSum += partialSum;
		}

		TestLoadResult result;
		result.sum          = totalSum;
		result.milliseconds = milliseconds;
		result.isCorrect    = (totalSum == GetExpectedTestLoadSum(elementCount));

		return result;
	}
} // namespace fang::editor
