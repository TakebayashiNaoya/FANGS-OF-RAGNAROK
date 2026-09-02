/**
 * @file JobSystem.cpp
 * @brief ジョブシステムの実装。ジョブプール、ワーカーの起床と就寝、スティール、依存の解決。
 */
#include "Pch.h"
#include "Core/Job/JobSystem.h"
#include "Core/CoreLog.h"
#include "Core/Job/JobCounter.h"
#include "Core/Job/WorkStealingDeque.h"
#include "Core/Memory/Allocator.h"
#include "Core/Platform/Thread.h"
#include <cstring>
#include <new>
#include <thread>


namespace fang
{
	namespace
	{
		/** @brief 寝る前に空回りする回数。すぐ次の仕事が来る場合に、起床の待ち時間を丸ごと省くため。 */
		constexpr uint32_t SPIN_COUNT_BEFORE_SLEEP = 64;

		/** @brief 添字と通し番号を 1 つの 64 bit にまとめる。通し番号は ABA よけ。 */
		constexpr uint64_t PackListHead(uint32_t index, uint32_t serialNumber)
		{
			return static_cast<uint64_t>(index) | (static_cast<uint64_t>(serialNumber) << 32);
		}


		constexpr uint32_t UnpackListIndex(uint64_t head)
		{
			return static_cast<uint32_t>(head & 0xFFFFFFFFu);
		}


		constexpr uint32_t UnpackListSerialNumber(uint64_t head)
		{
			return static_cast<uint32_t>(head >> 32);
		}


		/**
		 * @brief 配列を一括で確保して構築する。
		 * @details Core の New は 1 個ずつしか作れない。ワーカーは std::thread を持っていて自明な型ではないので、
		 *          確保はアロケータに任せたまま構築だけその場で行う。
		 */
		template <typename T> [[nodiscard]] T* NewArray(IAllocator& allocator, size_t count)
		{
			void* memory = allocator.Allocate(sizeof(T) * count, alignof(T));
			if (memory == nullptr)
			{
				return nullptr;
			}

			T* array = static_cast<T*>(memory);
			for (size_t i = 0; i < count; ++i)
			{
				::new (&array[i]) T();
			}

			return array;
		}


		template <typename T> void DeleteArray(IAllocator& allocator, T* array, size_t count)
		{
			if (array == nullptr)
			{
				return;
			}

			for (size_t i = count; i > 0; --i)
			{
				array[i - 1].~T();
			}

			allocator.Deallocate(array);
		}
	} // namespace


	/**
	 * @brief ジョブ 1 件。キャッシュライン 2 本ちょうどに収める。
	 * @details 引数は Submit がここへ写す。呼び出し側のメモリを持ち続けないので、数フレーム生きるジョブでも
	 *          積んだ側が先に消えてよい。
	 */
	struct JobSystem::Job
	{
		JobFunction function      = nullptr;
		JobCounter* finishCounter = nullptr;
		JobCounter* waitCounter   = nullptr;

		/** @brief 空きリストと保留リストのつなぎ。ジョブは同時に両方へは載らないので 1 本で足りる。 */
		std::atomic<uint32_t> nextIndex{ JobSystem::INVALID_JOB_INDEX };

		uint32_t argumentSize = 0;

		unsigned char arguments[JobSystem::MAX_ARGUMENT_SIZE]{};
	};


	/**
	 * @brief 実行に参加する 1 人分。0 番はメインスレッドの分で、スレッドを持たない。
	 * @threading deque の Push / Pop と randomState を触ってよいのは、この番号を名乗るスレッドだけ。
	 */
	// C4324（アラインメント指定子のために構造体がパッドされた）は、ここでは狙って詰め物を入れている。
#pragma warning(push)
#pragma warning(disable : 4324)
	struct alignas(CACHE_LINE_SIZE) JobSystem::Worker
	{
		/** @brief 次の犠牲者を選ぶための xorshift。毎回同じ順に当たると 1 人に集中するのを避ける。 */
		[[nodiscard]] uint32_t NextRandom()
		{
			randomState ^= randomState << 13;
			randomState ^= randomState >> 17;
			randomState ^= randomState << 5;
			return randomState;
		}

		WorkStealingDeque<uint32_t, JobSystem::DEQUE_CAPACITY> deque;

		std::thread thread;

		/**
		 * @brief このワーカーを名乗るスレッド。
		 * @details Initialize が全員分を書き終えてからでないとジョブは積まれないので、読む側との競合はない。
		 */
		std::thread::id threadId{};

		uint32_t randomState = 1;

#if FANG_ENABLE_PROFILER
		std::atomic<uint64_t> executedJobCount{ 0 };
#endif
	};
#pragma warning(pop)

	JobSystem::~JobSystem()
	{
		Shutdown();
	}


	bool JobSystem::Initialize(const JobSystemDesc& desc)
	{
		// Job と Worker は private なので、名前が見えるメンバ関数の中で確かめる。
		static_assert(sizeof(Job) == 128, "ジョブはキャッシュライン 2 本ちょうどに収める");
		static_assert(
			JobCounter::INVALID_JOB_INDEX == INVALID_JOB_INDEX,
			"保留リストと空きリストで空を表す値が食い違っている"
		);

		FANG_ASSERT(m_workers == nullptr, "ジョブシステムを二重に初期化している");

		uint32_t workerCount = desc.workerCount;
		if (workerCount == 0)
		{
			// メインスレッドも Wait の間は実行に参加するので、常駐ワーカーは 1 本減らす。
			const uint32_t coreCount = GetUsableCoreCount();
			workerCount              = coreCount > 1 ? coreCount - 1 : 1;
		}

		if (workerCount > MAX_WORKER_COUNT)
		{
			FANG_LOG_WARNING(Core, "ワーカー数 {} は多いので {} に丸めた", workerCount, MAX_WORKER_COUNT);
			workerCount = MAX_WORKER_COUNT;
		}

		IAllocator& allocator = HeapAllocator::GetInstance();

		// ジョブは実行中に増えない。ここで全部確保して、以降はフリーリストで回すだけにする。
		void* poolMemory = allocator.Allocate(sizeof(Job) * JOB_POOL_CAPACITY, CACHE_LINE_SIZE);
		if (poolMemory == nullptr)
		{
			FANG_LOG_ERROR(Core, "ジョブプールを確保できなかった ({} 件)", JOB_POOL_CAPACITY);
			return false;
		}

		m_jobPool = static_cast<Job*>(poolMemory);
		for (uint32_t i = 0; i < JOB_POOL_CAPACITY; ++i)
		{
			::new (&m_jobPool[i]) Job();
			const uint32_t nextIndex = (i + 1 < JOB_POOL_CAPACITY) ? (i + 1) : INVALID_JOB_INDEX;
			m_jobPool[i].nextIndex.store(nextIndex, std::memory_order_relaxed);
		}

		m_freeListHead.store(PackListHead(0, 0), std::memory_order_relaxed);

#if FANG_ENABLE_PROFILER
		m_jobsInUse.store(0, std::memory_order_relaxed);
		m_peakJobsInUse.store(0, std::memory_order_relaxed);
		m_inlineExecutedJobCount.store(0, std::memory_order_relaxed);
#endif

		m_workers = NewArray<Worker>(allocator, static_cast<size_t>(workerCount) + 1);
		if (m_workers == nullptr)
		{
			FANG_LOG_ERROR(Core, "ワーカーを確保できなかった ({} 人)", workerCount + 1);
			for (uint32_t i = 0; i < JOB_POOL_CAPACITY; ++i)
			{
				m_jobPool[i].~Job();
			}

			allocator.Deallocate(m_jobPool);
			m_jobPool = nullptr;
			return false;
		}

		m_workerCount   = workerCount;
		m_executorCount = workerCount + 1;

		for (uint32_t i = 0; i < m_executorCount; ++i)
		{
			// 種が 0 だと xorshift が 0 のまま動かないので、必ず 0 以外にする。
			m_workers[i].randomState = (i * 2654435761u) | 1u;
		}

		m_workers[MAIN_WORKER_INDEX].threadId = std::this_thread::get_id();

		m_runningThreadCount.store(0, std::memory_order_relaxed);
		m_isRunning.store(true, std::memory_order_release);

		for (uint32_t i = 1; i < m_executorCount; ++i)
		{
			m_workers[i].thread   = std::thread([this, i] { WorkerMain(i); });
			m_workers[i].threadId = m_workers[i].thread.get_id();
		}

		FANG_LOG_INFO(Core, "ジョブシステムを開始した (ワーカー数 {})", m_workerCount);

		return true;
	}


	void JobSystem::Shutdown()
	{
		if (m_workers == nullptr)
		{
			return;
		}

		m_isRunning.store(false, std::memory_order_release);
		WakeAllWorkers();

		for (uint32_t i = 1; i < m_executorCount; ++i)
		{
			if (m_workers[i].thread.joinable())
			{
				m_workers[i].thread.join();
			}
		}

		for (uint32_t i = 0; i < m_executorCount; ++i)
		{
			FANG_ASSERT(m_workers[i].deque.GetCount() == 0, "実行されていないジョブを残したまま Shutdown した");
		}

		IAllocator& allocator = HeapAllocator::GetInstance();

		DeleteArray(allocator, m_workers, m_executorCount);
		m_workers = nullptr;

		for (uint32_t i = 0; i < JOB_POOL_CAPACITY; ++i)
		{
			m_jobPool[i].~Job();
		}

		allocator.Deallocate(m_jobPool);
		m_jobPool = nullptr;

		m_workerCount   = 0;
		m_executorCount = 0;

		FANG_LOG_INFO(Core, "ジョブシステムを止めた");
	}


	void JobSystem::Submit(const JobDesc& desc, JobCounter* finishCounter)
	{
		FANG_ASSERT(m_isRunning.load(std::memory_order_acquire), "Initialize より前か Shutdown の後に Submit した");
		FANG_ASSERT(desc.function != nullptr, "ジョブの関数が空");
		FANG_ASSERT(
			desc.argumentSize <= MAX_ARGUMENT_SIZE,
			"引数が {} バイトある。{} バイトまで",
			desc.argumentSize,
			MAX_ARGUMENT_SIZE
		);

		// 積む前に増やす。積んでから増やすと、その間に前のジョブが終わって 0 になり、Wait が先に抜ける。
		if (finishCounter != nullptr)
		{
			IncrementCounter(*finishCounter);
		}

		const uint32_t workerIndex = FindWorkerIndex();
		const uint32_t jobIndex    = AllocateJob();
		if (jobIndex == INVALID_JOB_INDEX)
		{
			// 依存付きは今走らせると順序が壊れるし、空くのを待つとライブロックになる。回復手段がない。
			// 見るのは公開済みの完了でなく残り数。まだ 0 でないなら、このジョブは今は走れない。
			if (desc.waitCounter != nullptr && desc.waitCounter->GetValue() != 0)
			{
				FANG_FATAL("ジョブプールが満杯で、依存付きのジョブを積めない (上限 {} 件)", JOB_POOL_CAPACITY);
			}

			FANG_ASSERT(false, "ジョブプールが満杯 (上限 {} 件)。その場で実行して縮退する", JOB_POOL_CAPACITY);
			ExecuteInline(desc, finishCounter, workerIndex);
			return;
		}

		Job& job          = m_jobPool[jobIndex];
		job.function      = desc.function;
		job.finishCounter = finishCounter;
		job.waitCounter   = desc.waitCounter;
		job.argumentSize  = static_cast<uint32_t>(desc.argumentSize);
		if (desc.argumentSize > 0)
		{
			FANG_ASSERT(desc.arguments != nullptr, "引数の大きさだけ指定されていて中身が無い");
			std::memcpy(job.arguments, desc.arguments, desc.argumentSize);
		}

		if (desc.waitCounter != nullptr && TryPushPending(*desc.waitCounter, jobIndex))
		{
			// 積んだ直後に 0 になっていた場合、流す役が誰もいない。自分で拾い直して塞ぐ。
			// ここも残り数で見る。完了フラグで見ると、引き取り済みのリストに積んだ分が取り残される。
			if (desc.waitCounter->GetValue() == 0)
			{
				DispatchPendingList(ClaimPendingList(*desc.waitCounter), workerIndex);
			}

			return;
		}

		DispatchJob(jobIndex, workerIndex);
	}


	void JobSystem::Wait(JobCounter& counter)
	{
		if (counter.IsComplete())
		{
			return;
		}

		const uint32_t workerIndex = FindWorkerIndex();

		uint32_t spinCount = 0;
		while (!counter.IsComplete())
		{
			// 待っている間もこのスレッドは働く。ブロックしないので、待ちが増えてもコアが余らない。
			uint32_t jobIndex = INVALID_JOB_INDEX;
			if (workerIndex != INVALID_WORKER_INDEX && TryTakeJob(workerIndex, jobIndex))
			{
				ExecuteJob(jobIndex, workerIndex);
				spinCount = 0;
				continue;
			}

			if (spinCount < SPIN_COUNT_BEFORE_SLEEP)
			{
				++spinCount;
				std::this_thread::yield();
				continue;
			}

			// 仕事も無く、まだ終わっていない。寝る先はカウンタでなく JobSystem のチケット。カウンタは
			// 完了と同時に壊されてよいので、そこで寝ると死んだ番地で寝続けることになる。
			// 通し番号を控えてから最終確認する。控えた後に公開されたら番号が変わり、wait は素通りする。
			const uint32_t observedTicket = m_completionTicket.load(std::memory_order_acquire);
			if (counter.IsComplete())
			{
				break;
			}

			m_completionTicket.wait(observedTicket, std::memory_order_acquire);
			spinCount = 0;
		}
	}


	uint32_t JobSystem::GetRunningThreadCount() const
	{
		return m_runningThreadCount.load(std::memory_order_acquire);
	}


#if FANG_ENABLE_PROFILER
	uint64_t JobSystem::GetExecutedJobCount(uint32_t workerIndex) const
	{
		FANG_ASSERT(workerIndex < m_executorCount, "ワーカー番号 {} は範囲外", workerIndex);
		return m_workers[workerIndex].executedJobCount.load(std::memory_order_relaxed);
	}


	uint32_t JobSystem::GetJobsInUseCount() const
	{
		return m_jobsInUse.load(std::memory_order_relaxed);
	}


	uint32_t JobSystem::GetPeakJobsInUseCount() const
	{
		return m_peakJobsInUse.load(std::memory_order_relaxed);
	}


	void JobSystem::ResetPeakJobsInUseCount()
	{
		m_peakJobsInUse.store(m_jobsInUse.load(std::memory_order_relaxed), std::memory_order_relaxed);
	}


	uint64_t JobSystem::GetInlineExecutedJobCount() const
	{
		return m_inlineExecutedJobCount.load(std::memory_order_relaxed);
	}
#endif


	uint32_t JobSystem::FindWorkerIndex() const
	{
		const std::thread::id currentThreadId = std::this_thread::get_id();
		for (uint32_t i = 0; i < m_executorCount; ++i)
		{
			if (m_workers[i].threadId == currentThreadId)
			{
				return i;
			}
		}

		return INVALID_WORKER_INDEX;
	}


	uint32_t JobSystem::AllocateJob()
	{
		uint64_t head = m_freeListHead.load(std::memory_order_acquire);
		while (true)
		{
			const uint32_t jobIndex = UnpackListIndex(head);
			if (jobIndex == INVALID_JOB_INDEX)
			{
				return INVALID_JOB_INDEX;
			}

			const uint64_t nextHead = PackListHead(
				m_jobPool[jobIndex].nextIndex.load(std::memory_order_relaxed),
				UnpackListSerialNumber(head) + 1
			);
			if (m_freeListHead
					.compare_exchange_weak(head, nextHead, std::memory_order_acq_rel, std::memory_order_acquire))
			{
#if FANG_ENABLE_PROFILER
				// 増やした後の値が今の使用数。高水位は超えたときだけ書くので、普段は load 1 回で済む。
				const uint32_t jobsInUse     = m_jobsInUse.fetch_add(1, std::memory_order_relaxed) + 1;
				uint32_t       peakJobsInUse = m_peakJobsInUse.load(std::memory_order_relaxed);
				while (peakJobsInUse < jobsInUse &&
					   !m_peakJobsInUse.compare_exchange_weak(peakJobsInUse, jobsInUse, std::memory_order_relaxed))
				{
				}
#endif

				return jobIndex;
			}
		}
	}


	void JobSystem::FreeJob(uint32_t jobIndex)
	{
		uint64_t head = m_freeListHead.load(std::memory_order_relaxed);
		while (true)
		{
			m_jobPool[jobIndex].nextIndex.store(UnpackListIndex(head), std::memory_order_relaxed);

			const uint64_t nextHead = PackListHead(jobIndex, UnpackListSerialNumber(head) + 1);
			if (m_freeListHead
					.compare_exchange_weak(head, nextHead, std::memory_order_release, std::memory_order_relaxed))
			{
#if FANG_ENABLE_PROFILER
				m_jobsInUse.fetch_sub(1, std::memory_order_relaxed);
#endif

				return;
			}
		}
	}


	bool JobSystem::TryTakeJob(uint32_t workerIndex, uint32_t& outJobIndex)
	{
		Worker& worker = m_workers[workerIndex];
		if (worker.deque.Pop(outJobIndex))
		{
			return true;
		}

		if (m_executorCount <= 1)
		{
			return false;
		}

		// 自分の分が尽きたので他人から奪う。開始位置をずらさないと全員が同じ相手に群がる。
		const uint32_t startIndex = worker.NextRandom() % m_executorCount;
		for (uint32_t attempt = 0; attempt < m_executorCount; ++attempt)
		{
			const uint32_t victimIndex = (startIndex + attempt) % m_executorCount;
			if (victimIndex == workerIndex)
			{
				continue;
			}

			if (m_workers[victimIndex].deque.Steal(outJobIndex) == EnStealResult::Success)
			{
				return true;
			}
		}

		return false;
	}


	void JobSystem::ExecuteJob(uint32_t jobIndex, uint32_t workerIndex)
	{
		Job& job = m_jobPool[jobIndex];

		const JobFunction function      = job.function;
		JobCounter* const finishCounter = job.finishCounter;

		function(job.arguments, workerIndex);

		// 引数を読み終えてから枠を返す。返した後のジョブは、もう別の Submit のものかもしれない。
		FreeJob(jobIndex);

#if FANG_ENABLE_PROFILER
		m_workers[workerIndex].executedJobCount.fetch_add(1, std::memory_order_relaxed);
#endif

		if (finishCounter != nullptr)
		{
			DecrementCounter(*finishCounter, workerIndex);
		}
	}


	void JobSystem::ExecuteInline(const JobDesc& desc, JobCounter* finishCounter, uint32_t workerIndex)
	{
#if FANG_ENABLE_PROFILER
		// ここを通った分は executedJobCount に乗らないので、別に数えておかないとパネルから消える。
		m_inlineExecutedJobCount.fetch_add(1, std::memory_order_relaxed);
#endif

		// プールから取れなかったときの縮退。引数の寿命を揃えるため、ジョブと同じようにここへ写してから呼ぶ。
		alignas(8) unsigned char argumentBuffer[MAX_ARGUMENT_SIZE]{};
		if (desc.argumentSize > 0)
		{
			std::memcpy(argumentBuffer, desc.arguments, desc.argumentSize);
		}

		const uint32_t callerIndex = (workerIndex != INVALID_WORKER_INDEX) ? workerIndex : MAIN_WORKER_INDEX;
		desc.function(argumentBuffer, callerIndex);

		if (finishCounter != nullptr)
		{
			DecrementCounter(*finishCounter, workerIndex);
		}
	}


	void JobSystem::DispatchJob(uint32_t jobIndex, uint32_t workerIndex)
	{
		// 完了フラグでなく残り数で見る。引き取り役が公開を終える前に流された分も、依存は解けている。
		FANG_ASSERT(
			m_jobPool[jobIndex].waitCounter == nullptr || m_jobPool[jobIndex].waitCounter->GetValue() == 0,
			"依存が解けていないジョブを実行待ちに入れた"
		);

		if (workerIndex != INVALID_WORKER_INDEX && m_workers[workerIndex].deque.Push(jobIndex))
		{
			WakeOneWorker();
			return;
		}

		// キューが満杯か、キューを持たないスレッドから積まれた。並びは崩れるが結果は変わらない。
		if (workerIndex == INVALID_WORKER_INDEX)
		{
			FANG_ASSERT(false, "ワーカー以外のスレッドから積まれた。その場で実行して縮退する");
			ExecuteJob(jobIndex, MAIN_WORKER_INDEX);
			return;
		}

		FANG_ASSERT(
			false,
			"ワーカー {} のキューが満杯 (上限 {} 件)。その場で実行して縮退する",
			workerIndex,
			DEQUE_CAPACITY
		);
		ExecuteJob(jobIndex, workerIndex);
	}


	void JobSystem::DispatchPendingList(uint32_t firstJobIndex, uint32_t workerIndex)
	{
		uint32_t jobIndex = firstJobIndex;
		while (jobIndex != INVALID_JOB_INDEX)
		{
			// 積んだ先で即実行されると nextIndex が空きリストのものに書き換わるので、先に控える。
			const uint32_t nextIndex = m_jobPool[jobIndex].nextIndex.load(std::memory_order_relaxed);

			// 0 に達したカウンタへ続けて積む使い方があるため、引き取りと入れ違いに残り数が増え、
			// 依存の解けていないジョブを引き取ってしまうことがある。その分は保留リストへ戻す
			// （Submit と同じく、戻した直後の 0 到達は自分で拾い直す）。
			JobCounter* const waitCounter = m_jobPool[jobIndex].waitCounter;
			if (waitCounter != nullptr && TryPushPending(*waitCounter, jobIndex))
			{
				if (waitCounter->GetValue() == 0)
				{
					DispatchPendingList(ClaimPendingList(*waitCounter), workerIndex);
				}

				jobIndex = nextIndex;
				continue;
			}

			DispatchJob(jobIndex, workerIndex);
			jobIndex = nextIndex;
		}
	}


	bool JobSystem::TryPushPending(JobCounter& counter, uint32_t jobIndex)
	{
		// 残り数で見る。完了フラグで見ると、引き取りの済んだリストに積んでしまい取り残される。
		if (counter.GetValue() == 0)
		{
			return false;
		}

		uint64_t head = counter.m_pendingHead.load(std::memory_order_relaxed);
		while (true)
		{
			m_jobPool[jobIndex].nextIndex.store(UnpackListIndex(head), std::memory_order_relaxed);

			const uint64_t nextHead = PackListHead(jobIndex, UnpackListSerialNumber(head) + 1);
			if (counter.m_pendingHead
					.compare_exchange_weak(head, nextHead, std::memory_order_release, std::memory_order_relaxed))
			{
				return true;
			}
		}
	}


	uint32_t JobSystem::ClaimPendingList(JobCounter& counter)
	{
		// 空なら 1 度も書かずに帰る。カウンタが 0 になった後で書きに行く時間を短くしたい。
		uint64_t head = counter.m_pendingHead.load(std::memory_order_acquire);
		while (UnpackListIndex(head) != JobCounter::INVALID_JOB_INDEX)
		{
			const uint64_t emptyHead = PackListHead(JobCounter::INVALID_JOB_INDEX, UnpackListSerialNumber(head) + 1);
			if (counter.m_pendingHead
					.compare_exchange_weak(head, emptyHead, std::memory_order_acq_rel, std::memory_order_acquire))
			{
				return UnpackListIndex(head);
			}
		}

		return INVALID_JOB_INDEX;
	}


	void JobSystem::IncrementCounter(JobCounter& counter)
	{
		// 増やすのと完了フラグを下ろすのを 1 回の CAS で済ませる。別々にすると、下ろした後に前の代の
		// 引き取り役が完了を立て直し、走っているジョブを置いて Wait が抜ける。
		// 通し番号も一緒に進める。これがないと「0 ➡ 積む ➡ また 0」で状態語が元の値に戻り、公開しよう
		// としている引き取り役の CAS が通ってしまい、その間に積まれた保留ジョブが置き去りになる。
		uint64_t state     = counter.m_state.load(std::memory_order_relaxed);
		uint64_t nextState = 0;
		do
		{
			const uint64_t remainingCount = state & JobCounter::REMAINING_COUNT_MASK;
			FANG_ASSERT(remainingCount < JobCounter::REMAINING_COUNT_MASK, "1 つのカウンタで数えられる上限を超えた");

			nextState = (remainingCount + 1) | (state & JobCounter::DISPATCH_OWNER_FLAG) |
						((state & JobCounter::SUBMIT_SERIAL_MASK) + JobCounter::SUBMIT_SERIAL_UNIT);
		} while (!counter.m_state
					  .compare_exchange_weak(state, nextState, std::memory_order_acq_rel, std::memory_order_relaxed));
	}


	void JobSystem::DecrementCounter(JobCounter& counter, uint32_t workerIndex)
	{
		// 減らすのと「引き取り役を名乗る」のも 1 回の CAS で済ませる。別々にすると、名乗るまでの間に
		// 次の Submit と完了が走り抜けて、引き取り役が 2 人になる。
		uint64_t state           = counter.m_state.load(std::memory_order_relaxed);
		uint64_t nextState       = 0;
		bool     isDispatchOwner = false;
		do
		{
			const uint64_t remainingCount = state & JobCounter::REMAINING_COUNT_MASK;
			FANG_ASSERT(remainingCount > 0, "カウンタを 0 より下に減らした。Submit と完了の数が合っていない");
			FANG_ASSERT((state & JobCounter::COMPLETION_FLAG) == 0, "まだ残っているのに完了が公開されている");

			const uint64_t nextRemainingCount = remainingCount - 1;
			const uint64_t nextOwnerFlag =
				(nextRemainingCount == 0) ? JobCounter::DISPATCH_OWNER_FLAG : (state & JobCounter::DISPATCH_OWNER_FLAG);

			isDispatchOwner = (nextRemainingCount == 0) && ((state & JobCounter::DISPATCH_OWNER_FLAG) == 0);
			nextState       = nextRemainingCount | nextOwnerFlag | (state & JobCounter::SUBMIT_SERIAL_MASK);
		} while (!counter.m_state
					  .compare_exchange_weak(state, nextState, std::memory_order_acq_rel, std::memory_order_relaxed));

		if (!isDispatchOwner)
		{
			// 引き取り役が別にいる。ここでカウンタに触ると、公開された後の番地を触ることになる。
			return;
		}

		// 引き取り役。保留分を全部流してから完了を公開する。公開より後はカウンタに一切触らない。
		while (true)
		{
			DispatchPendingList(ClaimPendingList(counter), workerIndex);

			uint64_t       observedState  = counter.m_state.load(std::memory_order_acquire);
			const uint64_t remainingCount = observedState & JobCounter::REMAINING_COUNT_MASK;
			if (remainingCount != 0)
			{
				// 流している間に次の Submit が来た。役を降りて、次に 0 まで減らす人へ渡す。
				if (counter.m_state.compare_exchange_strong(
						observedState,
						remainingCount | (observedState & JobCounter::SUBMIT_SERIAL_MASK),
						std::memory_order_release,
						std::memory_order_relaxed
					))
				{
					return;
				}

				continue;
			}

			if (UnpackListIndex(counter.m_pendingHead.load(std::memory_order_acquire)) != JobCounter::INVALID_JOB_INDEX)
			{
				// 引き取った後に積まれた分がある。流しきるまで公開しない。
				continue;
			}


			// 通し番号ごと見比べるので、確かめてから公開するまでの間に Submit が挟まれば必ず外れる。
			if (counter.m_state.compare_exchange_strong(
					observedState,
					JobCounter::COMPLETION_FLAG | (observedState & JobCounter::SUBMIT_SERIAL_MASK),
					std::memory_order_release,
					std::memory_order_relaxed
				))
			{
				break;
			}
		}

		// 公開したのでカウンタはもう見ない。寝ている Wait は JobSystem 側のチケットで起こす。
		m_completionTicket.fetch_add(1, std::memory_order_release);
		m_completionTicket.notify_all();
	}


	void JobSystem::WorkerMain(uint32_t workerIndex)
	{
		char threadName[] = "FangJobWorker00";
		threadName[13]    = static_cast<char>('0' + (workerIndex / 10) % 10);
		threadName[14]    = static_cast<char>('0' + workerIndex % 10);
		SetCurrentThreadName(threadName);

		m_runningThreadCount.fetch_add(1, std::memory_order_release);

		while (m_isRunning.load(std::memory_order_acquire))
		{
			uint32_t jobIndex = INVALID_JOB_INDEX;
			if (TryTakeJob(workerIndex, jobIndex))
			{
				ExecuteJob(jobIndex, workerIndex);
				continue;
			}

			// すぐ次が来ることが多いので、寝る前に少しだけ起きたまま探す。
			bool hasFound = false;
			for (uint32_t spinCount = 0; spinCount < SPIN_COUNT_BEFORE_SLEEP; ++spinCount)
			{
				std::this_thread::yield();
				if (TryTakeJob(workerIndex, jobIndex))
				{
					hasFound = true;
					break;
				}
			}

			if (hasFound)
			{
				ExecuteJob(jobIndex, workerIndex);
				continue;
			}

			// 通し番号を控えてから、もう一度だけ探す。控えた後に積まれたら番号が変わり、wait は素通りする。
			const uint32_t observedTicket = m_submitTicket.load(std::memory_order_acquire);
			if (TryTakeJob(workerIndex, jobIndex))
			{
				ExecuteJob(jobIndex, workerIndex);
				continue;
			}

			if (!m_isRunning.load(std::memory_order_acquire))
			{
				break;
			}

			m_submitTicket.wait(observedTicket, std::memory_order_acquire);
		}

		m_runningThreadCount.fetch_sub(1, std::memory_order_release);
	}


	void JobSystem::WakeOneWorker()
	{
		m_submitTicket.fetch_add(1, std::memory_order_release);
		m_submitTicket.notify_one();
	}


	void JobSystem::WakeAllWorkers()
	{
		m_submitTicket.fetch_add(1, std::memory_order_release);
		m_submitTicket.notify_all();
	}
} // namespace fang
