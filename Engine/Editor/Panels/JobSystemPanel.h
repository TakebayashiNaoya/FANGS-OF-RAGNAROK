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
	 * @details 実行者ごとの実行数とジョブプールの使用量を出し、入れている間ずっと負荷をかけるチェックを持つ。
	 *          負荷は同じ本体を ParallelFor と SerialFor の両方で回し、所要時間を並べて出す。
	 *          並列化で何倍になるか、どの粒度から積み下ろしの元が取れるかを、要素数と分割幅を動かしながら
	 *          見るための道具。
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
		 * @brief 負荷を回す設定なら、そのフレームの負荷を並列と直列の両方で積み、総和を検算する。
		 * @param frameIndex 更新しているフレームの番号。
		 * @threading 更新ジョブの中（ワーカースレッド）から呼ぶ。ImGui には触らない。
		 */
		void RunRequestedTestLoad(uint64_t frameIndex);


	private:
		/** @brief 更新しているフレームと、描いている 1 つ前で 2 面。 */
		static constexpr size_t SLOT_COUNT = 2;

		/** @brief 所要時間の移動平均に使う履歴の長さ（回数）。 */
		static constexpr uint32_t TEST_LOAD_HISTORY_LENGTH = 120;

		/** @brief 負荷を 1 回回した結果。並列版と直列版で同じ形を使う。 */
		struct TestLoadResult
		{
			uint64_t sum          = 0;     /**< 出た総和。 */
			float    milliseconds = 0.0f;  /**< 回すのにかかった時間。総和の集計は含まない。 */
			bool     isCorrect    = false; /**< 総和が期待値と一致したか。 */
		};

		/**
		 * @brief テスト負荷 1 フレーム分。
		 * @details 描画側が条件と回すかどうかを書き、更新側が結果を書く。同じ周の更新と描画がぶつからないよう、
		 *          フレームの偶奇で 2 面持ち、走っている更新が触らないほうだけを描画側が触る。
		 */
		struct TestLoadSlot
		{
			TestLoadResult parallel;              /**< ParallelFor で回した結果。 */
			TestLoadResult serial;                /**< SerialFor で回した結果。 */
			uint64_t       runFrameIndex = 0;     /**< 結果を出したフレームの番号。二重に平均へ積まない印。 */
			uint32_t       elementCount  = 0;     /**< この面を回したときの要素数。 */
			uint32_t       batchSize     = 0;     /**< この面を回したときの分割幅。 */
			bool           isSerialFirst = false; /**< 直列版を先に回したか。 */
			bool           isRequested   = false; /**< 次の更新に負荷を積ませるか。 */
			bool           hasRun        = false; /**< 一度でも回したか。回すまで結果は出さない。 */
		};

		/**
		 * @brief 所要時間の移動平均。
		 * @details 1 回ごとの実測は跳ねて読めないので均す。
		 *          押し出す値を和から引く ➡ 新しい値を書いて和に足す ➡ 書き込み位置を進める、の O(1)。
		 * @threading 描画側（メインスレッド）だけが触る。
		 */
		struct MillisecondsAverage
		{
			/** @brief 1 件積む。 */
			void Push(float milliseconds);

			/** @brief 今の平均。まだ 1 件も無ければ 0。 */
			[[nodiscard]] float GetAverage() const;

			/** @brief 空に戻す。条件を変えたときに、前の条件で出た値を混ぜないために呼ぶ。 */
			void Reset();

			float    history[TEST_LOAD_HISTORY_LENGTH]{}; /**< リングバッファ。 */
			uint32_t writeIndex            = 0;           /**< 次に書き込む位置。 */
			uint32_t sampleCount           = 0;           /**< 埋まった数。押し始めは分母をこれに絞る。 */
			float    movingSumMilliseconds = 0.0f;        /**< 履歴の合計。サンプル数で割ると移動平均。 */
		};

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

		/**
		 * @brief テスト負荷の区画を組み立てる。
		 * @param drawingFrameIndex 描画側が触ってよい面のフレーム番号。走っている更新の 1 つ先。
		 */
		void BuildTestLoadSection(uint64_t drawingFrameIndex);

		/** @brief 負荷を回すチェックと、要素数・分割幅のスライダを組み立てる。動かしたら移動平均を空に戻す。 */
		void BuildTestLoadParameters();

		/** @brief 移動平均がまだ空のとき、なぜ数字が出ないのかを 1 行で出す。 */
		void BuildTestLoadWaitingText() const;

		/** @brief 面に載っている結果を移動平均へ引き取る。まだ回していない面と、条件の違う面は捨てる。 */
		void PushTestLoadSamples(const TestLoadSlot& slot);

		/** @brief 並列版と直列版を並べた表と、そこから出した倍率を組み立てる。 */
		void BuildTestLoadComparisonTable() const;

		/** @brief 比較の表の 1 行を組み立てる。 */
		void BuildTestLoadRow(const char* label, const TestLoadResult& result, float averageMilliseconds) const;

		/** @brief 並列版と直列版を両方回して面へ書く。どちらを先にするかは面が持っている。 */
		void RunTestLoad(TestLoadSlot& slot);

		/** @brief ParallelFor で 1 回分の負荷を回す。 */
		[[nodiscard]] TestLoadResult RunParallelTestLoad(uint32_t elementCount, uint32_t batchSize);

		/** @brief SerialFor で 1 回分の負荷を回す。本体も添字をたどる順も並列版と同じ。 */
		[[nodiscard]] TestLoadResult RunSerialTestLoad(uint32_t elementCount);

		/** @brief 部分和を 0 に戻す。 */
		void ClearTestLoadPartialSums();

		/** @brief 部分和を足し合わせ、期待値と突き合わせて 1 回分の結果にする。 */
		[[nodiscard]] TestLoadResult MakeTestLoadResult(float milliseconds, uint32_t elementCount) const;


	private:
		JobSystem* m_jobSystem = nullptr; /**< RunApplication が持っているもの。ここでは借りるだけ。 */

#if FANG_ENABLE_PROFILER
		/** @brief 区切りの頭で控えた累計。ここからの差分が区間の実行数になる。 */
		uint64_t m_previousExecutedJobCounts[JobSystem::MAX_WORKER_COUNT + 1]{};

		/** @brief 直前の区間から出した 1 秒あたりの実行数。次の区切りまで動かさない。 */
		float m_executedJobCountsPerSecond[JobSystem::MAX_WORKER_COUNT + 1]{};

		float m_elapsedSecondsInInterval = 0.0f; /**< 今の区切りが始まってからの秒数。 */
#endif

		/** @brief 負荷が足し合わせる配列。実行中に確保しないよう Initialize で 1 回だけ上限まで伸ばす。 */
		std::vector<uint32_t> m_testLoadValues;

		/** @brief ワーカー番号で引く部分和。ジョブは自分の番号の枠だけ触る。 */
		uint64_t m_testLoadPartialSums[JobSystem::MAX_WORKER_COUNT + 1]{};

		TestLoadSlot m_testLoadSlots[SLOT_COUNT]; /**< フレームの偶奇で選ぶ 2 面。 */

		uint32_t m_testLoadElementCount = 0; /**< 次に回す要素数。Initialize が既定値を入れ、スライダが動かす。 */
		uint32_t m_testLoadBatchSize    = 0; /**< 次に回す分割幅。同じく Initialize とスライダが決める。 */

		/**
		 * @brief 負荷を回し続けるか。
		 * @details 押している間だけのボタンにすると、回しながらスライダを動かせない。
		 *          手も入力の焦点も 1 つしかないので、条件を変えた瞬間に負荷が止まり、移動平均が空のまま埋まらない。
		 */
		bool m_isTestLoadRunning = false;

		MillisecondsAverage m_parallelAverage; /**< 並列版の所要時間の移動平均。 */
		MillisecondsAverage m_serialAverage;   /**< 直列版の所要時間の移動平均。 */

		/** @brief 移動平均へ引き取った最後の面のフレーム番号。同じ面を二重に積まないための印。 */
		uint64_t m_lastAveragedFrameIndex = UINT64_MAX;

		TestLoadResult m_lastParallelResult; /**< 引き取った最後の並列版の結果。表はこれを出す。 */
		TestLoadResult m_lastSerialResult;   /**< 同じく直列版の結果。 */
	};
} // namespace fang::editor
