/**
 * @file JobSystemPanel.h
 * @brief ジョブシステムの稼働状況を出すパネル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Job/JobSystem.h"
#include <cstddef>
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
	 * @details 実行者ごとの実行数とジョブプールの使用量を出し、押している間だけ負荷をかけるボタンを持つ。
	 *          負荷を積むのは更新ジョブの中なので、更新と描画が重なって回っていることが所要時間に出る。
	 * @threading 組み立てはメインスレッドのみ。RunRequestedTestLoad だけが更新ジョブの中で走る。
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
		 * @param deltaTimeSeconds   前フレームからの経過時間（秒）。毎秒の実行数を区切るのに使う。
		 * @param updatingFrameIndex 今そこで走っている更新のフレーム番号。触る面を選ぶのに使う。
		 */
		void BuildFrame(float deltaTimeSeconds, uint64_t updatingFrameIndex);

		/**
		 * @brief ボタンが押されていれば、そのフレームの負荷を積んで総和を検算する。
		 * @param frameIndex 更新しているフレームの番号。
		 * @threading 更新ジョブの中（ワーカースレッド）から呼ぶ。ImGui には触らない。
		 */
		void RunRequestedTestLoad(uint64_t frameIndex);


	private:
		/**
		 * @brief テスト負荷 1 フレーム分。
		 * @details 描画側がボタンの状態を書き、更新側が結果を書く。同じ周の更新と描画がぶつからないよう、
		 *          フレームの偶奇で 2 面持ち、走っている更新が触らないほうだけを描画側が触る。
		 */
		struct TestLoadSlot
		{
			uint64_t sum         = 0;     /**< 更新側が出した総和。 */
			bool     isRequested = false; /**< 次の更新に負荷を積ませるか。 */
			bool     hasRun      = false; /**< 一度でも回したか。回すまで検算結果は出さない。 */
			bool     isCorrect   = false; /**< 総和が期待値と一致したか。 */
		};

		static constexpr size_t SLOT_COUNT = 2; /**< 更新しているフレームと、描いている 1 つ前で 2 面。 */

		/** @brief フレーム番号から触ってよい面を決める。 */
		[[nodiscard]] static FANG_FORCEINLINE size_t GetSlotIndex(uint64_t frameIndex)
		{
			return static_cast<size_t>(frameIndex % SLOT_COUNT);
		}

#if FANG_ENABLE_PROFILER
		/** @brief 0.5 秒の区切りごとに毎秒の実行数を出し直す。毎フレーム更新だと数字が跳ねて読めない。 */
		void UpdateExecutedJobCountsPerSecond(float deltaTimeSeconds);

		/** @brief 実行者ごとの表（累計と毎秒の実行数）を組み立てる。 */
		void BuildExecutorTable();

		/** @brief ジョブプールの使用量と高水位の区画を組み立てる。 */
		void BuildJobPoolSection();
#endif

		/** @brief テスト負荷のボタンと検算結果の区画を組み立てる。 */
		void BuildTestLoadSection(size_t slotIndex);

		/** @brief ParallelFor で 1 フレーム分の負荷を積み、総和を検算して面へ書く。 */
		void RunTestLoad(TestLoadSlot& slot);


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

		TestLoadSlot m_testLoadSlots[SLOT_COUNT]; /**< フレームの偶奇で選ぶ 2 面。 */
	};
} // namespace fang::editor
