/**
 * @file Budget.cpp
 * @brief 予算の現在値の更新と、Xbox 換算での待ち。
 */
#include "Pch.h"
#include "Core/Platform/Budget.h"
#include "Core/CoreLog.h"
#include "Core/Platform/MemoryUsage.h"
#include <algorithm>
#include <chrono>
#include <thread>


namespace fang
{
	namespace
	{
		/** @brief メモリを測り直す間隔（フレーム）。毎フレーム OS に聞くほどの精度は要らない。 */
		constexpr uint32_t MEMORY_SAMPLE_INTERVAL_FRAMES = 60;

		/**
		 * @brief 警告を出し直せるようになる割合。
		 * @details 境目で行き来したときに、同じ警告が何度も出るのを防ぐための戻り幅。
		 */
		constexpr float MEMORY_WARNING_RELEASE_RATIO = 0.75f;

		/** @brief この時間だけは sleep に任せず回して待つ。sleep の分解能はミリ秒単位で粗いため。 */
		constexpr float SPIN_MARGIN_SECONDS = 0.002f;


		/** @brief バイトを MiB に直す。ログに出すためだけの変換。 */
		[[nodiscard]] double ToMebibytes(uint64_t bytes)
		{
			return static_cast<double>(bytes) / (1024.0 * 1024.0);
		}
	} // namespace


	void PlatformBudget::SetCpuScaleFactor(float scaleFactor)
	{
		m_cpuScaleFactor = std::clamp(scaleFactor, budget::MINIMUM_CPU_SCALE_FACTOR, budget::MAXIMUM_CPU_SCALE_FACTOR);
	}


	void PlatformBudget::EndFrame(float frameWorkSeconds)
	{
		m_frameWorkSeconds = frameWorkSeconds;

		if (m_framesUntilMemorySample == 0)
		{
			SampleMemoryUsage();
			m_framesUntilMemorySample = MEMORY_SAMPLE_INTERVAL_FRAMES;
		}
		else
		{
			--m_framesUntilMemorySample;
		}

		// 待つのは最後。ここまでの処理は実処理時間に数えない。
		if (m_isThrottleEnabled)
		{
			WaitForScaledFrame(frameWorkSeconds);
		}
	}


	void PlatformBudget::SampleMemoryUsage()
	{
		const MemoryUsage usage = GetProcessMemoryUsage();
		if (usage.usedBytes == 0)
		{
			return;
		}

		m_memoryUsedBytes        = usage.usedBytes;
		m_systemMemoryLimitBytes = usage.limitBytes;
		m_memoryPeakBytes        = std::max(m_memoryPeakBytes, usage.usedBytes);

		const double usedRatio = static_cast<double>(usage.usedBytes) / static_cast<double>(budget::MEMORY_LIMIT_BYTES);

		// 下がったら次に超えたときまた知らせる。出しっぱなしにすると気付けない。
		if (usedRatio < static_cast<double>(MEMORY_WARNING_RELEASE_RATIO))
		{
			m_hasWarnedMemoryNearLimit = false;
			m_hasWarnedMemoryOverLimit = false;
			return;
		}

		if (usedRatio >= 1.0 && !m_hasWarnedMemoryOverLimit)
		{
			FANG_LOG_WARNING(
				Core,
				"Xbox の予算 {:.0f} MiB を超えた。今 {:.1f} MiB / 最高 {:.1f} MiB",
				ToMebibytes(budget::MEMORY_LIMIT_BYTES),
				ToMebibytes(usage.usedBytes),
				ToMebibytes(m_memoryPeakBytes)
			);

			m_hasWarnedMemoryOverLimit = true;
			m_hasWarnedMemoryNearLimit = true;
			return;
		}

		if (usedRatio >= static_cast<double>(budget::MEMORY_WARNING_RATIO) && !m_hasWarnedMemoryNearLimit)
		{
			FANG_LOG_WARNING(
				Core,
				"Xbox の予算の {:.0f}% を使っている。今 {:.1f} MiB / 上限 {:.0f} MiB",
				usedRatio * 100.0,
				ToMebibytes(usage.usedBytes),
				ToMebibytes(budget::MEMORY_LIMIT_BYTES)
			);

			m_hasWarnedMemoryNearLimit = true;
		}
	}


	void PlatformBudget::WaitForScaledFrame(float frameWorkSeconds) const
	{
		// 実処理はもう終わっているので、換算時間との差だけ待てば合計が換算時間になる。
		const float waitSeconds = frameWorkSeconds * (m_cpuScaleFactor - 1.0f);
		if (waitSeconds <= 0.0f)
		{
			return;
		}

		using Clock    = std::chrono::steady_clock;
		using Duration = std::chrono::duration<float>;

		const Clock::time_point deadline =
			Clock::now() + std::chrono::duration_cast<Clock::duration>(Duration(waitSeconds));

		if (waitSeconds > SPIN_MARGIN_SECONDS)
		{
			std::this_thread::sleep_for(Duration(waitSeconds - SPIN_MARGIN_SECONDS));
		}

		// 残りは回して待つ。ここを sleep に任せると分解能の分だけ余計に眠って換算時間から外れる。
		while (Clock::now() < deadline)
		{
			std::this_thread::yield();
		}
	}
} // namespace fang
