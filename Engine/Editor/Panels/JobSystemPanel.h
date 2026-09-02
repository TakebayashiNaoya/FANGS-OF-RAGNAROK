/**
 * @file JobSystemPanel.h
 * @brief ジョブシステムの稼働状況を出すパネル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Job/JobSystem.h"
#include <cstdint>
#include <vector>


namespace fang
{
	struct EngineContext;
} // namespace fang


namespace fang::editor
{
	/**
	 * @brief 「ジョブシステム」ウィンドウ 1 枚。
	 * @details 実行者ごとの実行数とジョブプールの使用量を出し、押している間だけジョブを積むボタンを持つ。
	 *          フレームループがまだジョブを積まないので、動いている証拠を見る窓はここしかない。
	 * @threading メインスレッドのみ。
	 */
	class JobSystemPanel
	{
	public:
		FANG_NON_COPYABLE(JobSystemPanel);

		JobSystemPanel()  = default;
		~JobSystemPanel() = default;

		/**
		 * @brief 表示と負荷に使う入れ物を確保する。
		 * @param context 中身は呼び出し側が生かし続けるので、ジョブシステムの参照だけ控える。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(const EngineContext& context);

		/** @brief 控えたものを手放す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief このフレームの内容を組み立てる。ImGui::NewFrame の後に呼ぶ。
		 * @param deltaTimeSeconds 前フレームからの経過時間（秒）。毎秒の実行数を区切るのに使う。
		 */
		void BuildFrame(float deltaTimeSeconds);


	private:
#if FANG_ENABLE_PROFILER
		void UpdateExecutedJobCountsPerSecond(float deltaTimeSeconds);
		void BuildExecutorTable();
		void BuildJobPoolSection();
#endif

		void BuildTestLoadSection();
		void RunTestLoad();


	private:
		JobSystem* m_jobSystem = nullptr; /**< RunApplication が持っているもの。ここでは借りるだけ。 */

#if FANG_ENABLE_PROFILER
		/** @brief 区切りの頭で控えた累計。ここからの差分が区間の実行数になる。 */
		uint64_t m_previousExecutedJobCounts[JobSystem::MAX_WORKER_COUNT + 1]{};

		/** @brief 直前の区間から出した 1 秒あたりの実行数。次の区切りまで動かさない。 */
		float m_executedJobCountsPerSecond[JobSystem::MAX_WORKER_COUNT + 1]{};

		float m_elapsedSecondsInInterval = 0.0f; /**< 今の区切りが始まってからの秒数。 */
#endif

		/** @brief 負荷が足し合わせる配列。実行中に確保しないよう Initialize で 1 回だけ伸ばす。 */
		std::vector<uint32_t> m_testLoadValues;

		/** @brief ワーカー番号で引く部分和。ジョブは自分の番号の枠だけ触る。 */
		uint64_t m_testLoadPartialSums[JobSystem::MAX_WORKER_COUNT + 1]{};

		uint64_t m_testLoadSum       = 0;     /**< 直近の負荷が出した総和。 */
		bool     m_hasRunTestLoad    = false; /**< 一度でも負荷を回したか。回すまで検算結果は出さない。 */
		bool     m_isTestLoadCorrect = false; /**< 総和が期待値と一致したか。 */
	};
} // namespace fang::editor
