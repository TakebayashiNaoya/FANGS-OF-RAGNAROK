/**
 * @file JobSystem.h
 * @brief ワークスティーリング方式のジョブシステム。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Job/WorkStealingDeque.h"
#include <atomic>
#include <cstddef>
#include <cstdint>


namespace fang
{
	class JobCounter;

	/**
	 * @brief ジョブとして走る関数。
	 * @param arguments   Submit がコピーした引数。ジョブの実行中だけ有効。
	 * @param workerIndex 実行中のワーカー番号。0 はメインスレッド。スレッド固有の配列を引くのに使う。
	 */
	using JobFunction = void (*)(void* arguments, uint32_t workerIndex);

	/** @brief ジョブシステムの生成条件。 */
	struct JobSystemDesc
	{
		uint32_t workerCount = 0; /**< 常駐ワーカーの本数。0 なら物理コア数 − 1（Xbox は 5）。 */
	};

	/** @brief 積むジョブ 1 件の内容。 */
	struct JobDesc
	{
		JobFunction function     = nullptr;
		const void* arguments    = nullptr; /**< Submit がコピーするので、戻った後は解放してよい。 */
		size_t      argumentSize = 0;       /**< バイト数。JobSystem::MAX_ARGUMENT_SIZE まで。 */
		JobCounter* waitCounter  = nullptr; /**< 0 になるまで実行しない。省略可。 */
	};

	/**
	 * @brief ジョブを全コアに配って実行するシステム。
	 * @details ワーカーごとに両端キューを持ち、自分の分が尽きたら他人から奪う。メインスレッドは常駐
	 *          ワーカーにせず、Wait / ParallelFor の間だけワーカー番号 0 として実行に参加する。
	 * @threading Submit / Wait はメインスレッドとジョブの中から呼べる。それ以外のスレッドは自分の
	 *            キューを持たないので、Submit がその場で実行する縮退になる。Initialize / Shutdown は
	 *            メインスレッドのみ。
	 */
	class JobSystem
	{
	public:
		FANG_NON_COPYABLE(JobSystem);
		FANG_NON_MOVABLE(JobSystem);

		static constexpr uint32_t MAX_WORKER_COUNT     = 31;   /**< メインを足して 32 人まで。 */
		static constexpr size_t   MAX_ARGUMENT_SIZE    = 96;   /**< ジョブ 1 件が 128 バイトに収まる上限。 */
		static constexpr uint32_t JOB_POOL_CAPACITY    = 8192; /**< 全ワーカーで共有するジョブの総数。 */
		static constexpr uint32_t DEQUE_CAPACITY       = 4096; /**< ワーカー 1 人が抱えられるジョブ数。 */
		static constexpr uint32_t MAIN_WORKER_INDEX    = 0;    /**< メインスレッドが名乗るワーカー番号。 */
		static constexpr uint32_t INVALID_JOB_INDEX    = 0xFFFFFFFFu;
		static constexpr uint32_t INVALID_WORKER_INDEX = 0xFFFFFFFFu;

		JobSystem() = default;
		~JobSystem();

		/** @brief 常駐ワーカーの本数。メインスレッドは含まない。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetWorkerCount() const { return m_workerCount; }

		/** @brief 実行に参加しうるスレッドの数。メインを含むので GetWorkerCount() + 1。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetExecutorCount() const { return m_executorCount; }


	public:
		/**
		 * @brief ワーカーを起こし、ジョブの置き場を確保する。
		 * @param desc workerCount が 0 なら物理コア数から決める。
		 * @return 失敗したら false。ワーカーは 1 本も残らない。
		 */
		[[nodiscard]] bool Initialize(const JobSystemDesc& desc);

		/**
		 * @brief ワーカーを畳んで置き場を返す。二重に呼んでも安全。
		 * @details 実行中のジョブが終わるまでは戻らないが、まだ実行していないジョブは捨てる。
		 *          積んだジョブを Wait で全部回収してから呼ぶこと。
		 */
		void Shutdown();

		/**
		 * @brief ジョブを 1 件積む。
		 * @param desc          function は必須。argumentSize は MAX_ARGUMENT_SIZE まで。
		 * @param finishCounter 完了時に 1 減らすカウンタ。この関数が戻る前に 1 増える。省略可。
		 */
		void Submit(const JobDesc& desc, JobCounter* finishCounter);

		/**
		 * @brief カウンタが 0 になるまで待つ。待つ間、このスレッドも他のジョブを実行する。
		 * @param counter 呼ぶ側が 0 になるまで生かしておくこと。
		 */
		void Wait(JobCounter& counter);

		/** @brief 生きているワーカースレッドの数。Shutdown 後は 0。 */
		[[nodiscard]] uint32_t GetRunningThreadCount() const;

#if FANG_ENABLE_PROFILER
		/** @brief そのワーカーがこれまでに実行したジョブ数。 */
		[[nodiscard]] uint64_t GetExecutedJobCount(uint32_t workerIndex) const;

		/** @brief 今プールから出ているジョブの数。 */
		[[nodiscard]] uint32_t GetJobsInUseCount() const;

		/** @brief 起動（またはリセット）以降の同時使用数の最大。 */
		[[nodiscard]] uint32_t GetPeakJobsInUseCount() const;

		/** @brief 高水位を今の使用数まで戻す。メインスレッドのみ。 */
		void ResetPeakJobsInUseCount();

		/** @brief プールが満杯でその場実行に縮退した回数。0 でないなら詰まっている。 */
		[[nodiscard]] uint64_t GetInlineExecutedJobCount() const;
#endif


	private:
		struct Job;
		struct Worker;

		[[nodiscard]] uint32_t FindWorkerIndex() const;
		[[nodiscard]] uint32_t AllocateJob();
		void                   FreeJob(uint32_t jobIndex);
		[[nodiscard]] bool     TryTakeJob(uint32_t workerIndex, uint32_t& outJobIndex);
		void                   ExecuteJob(uint32_t jobIndex, uint32_t workerIndex);
		void                   ExecuteInline(const JobDesc& desc, JobCounter* finishCounter, uint32_t workerIndex);
		void                   DispatchJob(uint32_t jobIndex, uint32_t workerIndex);
		void                   DispatchPendingList(uint32_t firstJobIndex, uint32_t workerIndex);
		[[nodiscard]] bool     TryPushPending(JobCounter& counter, uint32_t jobIndex);
		[[nodiscard]] uint32_t ClaimPendingList(JobCounter& counter);
		void                   DecrementCounter(JobCounter& counter, uint32_t workerIndex);
		void                   WorkerMain(uint32_t workerIndex);
		void                   WakeOneWorker();
		void                   WakeAllWorkers();

		Worker* m_workers = nullptr; /**< 0 番はメインスレッドの分。スレッドを持たない。 */
		Job*    m_jobPool = nullptr; /**< 固定長。実行中はここから増えも減りもしない。 */

		uint32_t m_workerCount   = 0;
		uint32_t m_executorCount = 0;

		/** @brief 空きジョブの片方向リスト。下位 32 bit が先頭の添字、上位 32 bit が ABA よけの通し番号。 */
		std::atomic<uint64_t> m_freeListHead{ INVALID_JOB_INDEX };

#if FANG_ENABLE_PROFILER
		/** @brief 統計を空きリストと同じキャッシュラインに載せないための詰め物。 */
		unsigned char m_statisticsPadding[CACHE_LINE_SIZE]{};

		std::atomic<uint32_t> m_jobsInUse{ 0 };
		std::atomic<uint32_t> m_peakJobsInUse{ 0 };

		std::atomic<uint64_t> m_inlineExecutedJobCount{ 0 }; /**< プールが満杯でその場実行に縮退した回数。 */
#endif

		/**
		 * @brief Submit のたびに増える通し番号。
		 * @details 眠っているワーカーはこれに atomic::wait する。bool の起床フラグだと「無いことを
		 *          確かめてから寝るまで」の隙間で起こし損ねるが、値が変わっていれば wait は素通りする。
		 */
		std::atomic<uint32_t> m_submitTicket{ 0 };

		std::atomic<uint32_t> m_runningThreadCount{ 0 };
		std::atomic<bool>     m_isRunning{ false };
	};
} // namespace fang
