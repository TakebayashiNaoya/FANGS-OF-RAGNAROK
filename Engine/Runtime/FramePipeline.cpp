/**
 * @file FramePipeline.cpp
 * @brief 1 周の中身。更新の Submit、1 つ前のフレームの描画、更新の完了待ち。
 */
#include "Pch.h"
#include "Runtime/FramePipeline.h"
#include "Core/Job/JobSystem.h"
#include "Core/Memory/FrameAllocator.h"
#include "Input/Gamepad.h"
#include <chrono>


namespace fang
{
	namespace
	{
		/**
		 * @brief 更新ジョブへ写す引数。
		 * @details JobSystem が写せるのは 96 バイトまでなので、束ねるものは全部ポインタで持つ。
		 */
		struct UpdateJobArguments
		{
			FramePipeline*  pipeline         = nullptr;
			FrameAllocator* frameAllocator   = nullptr;
			uint64_t        frameIndex       = 0;
			float           deltaTimeSeconds = 0.0f;
			GamepadState    gamepad;
		};

		static_assert(
			sizeof(UpdateJobArguments) <= JobSystem::MAX_ARGUMENT_SIZE,
			"更新ジョブの引数が JobSystem の写せる大きさを超えている"
		);

#if FANG_ENABLE_PROFILER

		/** @brief 控えておいた時点から今までの時間をミリ秒で返す。 */
		float GetElapsedMilliseconds(const std::chrono::steady_clock::time_point& beginTime)
		{
			const auto endTime = std::chrono::steady_clock::now();
			return std::chrono::duration<float, std::milli>(endTime - beginTime).count();
		}

#endif
	} // namespace


	bool FramePipeline::Initialize(
		JobSystem&     jobSystem,
		FrameMemory&   frameMemory,
		void*          userData,
		UpdateFunction updateFunction,
		RenderFunction renderFunction
	)
	{
		FANG_ASSERT(m_updateFunction == nullptr, "FramePipeline を二重に初期化している");

		if (updateFunction == nullptr || renderFunction == nullptr)
		{
			return false;
		}

		m_jobSystem   = &jobSystem;
		m_frameMemory = &frameMemory;

		m_userData       = userData;
		m_updateFunction = updateFunction;
		m_renderFunction = renderFunction;

		return true;
	}


	void FramePipeline::Shutdown()
	{
		FANG_ASSERT(IsUpdateComplete(), "更新ジョブを回収する前に FramePipeline を畳もうとしている");

		m_jobSystem   = nullptr;
		m_frameMemory = nullptr;

		m_userData       = nullptr;
		m_updateFunction = nullptr;
		m_renderFunction = nullptr;

		m_frameData[0] = nullptr;
		m_frameData[1] = nullptr;

		m_frameIndex = 0;
		m_isPrimed   = false;
	}


	void FramePipeline::Prime()
	{
		FANG_ASSERT(m_updateFunction != nullptr, "FramePipeline が初期化されていない");
		FANG_ASSERT(!m_isPrimed, "助走を二重に走らせている");

		// まだ描く相手がいないので、フレーム 0 の更新だけはジョブにせずここで済ませる。
		// ➡ ループの中に「初回だけ」の分岐が 1 つも要らなくなる。
		m_frameMemory->BeginFrame();
		RunUpdate(m_frameMemory->GetCurrent(), 0, 0.0f, GamepadState{});

		m_frameIndex = 0;
		m_isPrimed   = true;
	}


	void FramePipeline::RunFrame(float deltaTimeSeconds, const GamepadState& gamepad)
	{
		FANG_ASSERT(m_isPrimed, "助走を走らせないまま 1 周を回そうとしている");

		// ① 面を交換する。
		// 　 更新と描画が同時に触る面が重ならないよう、面の入れ替えはここメインスレッドだけが握る。
		// 直前の更新が書いた面が「前の面」へ移り、「今の面」が空く。面の入れ替えを握るのはメインだけ。
		m_frameMemory->BeginFrame();

		// ② 更新ジョブを投入する。
		// 　 今の m_frameIndex を「これから描く番号」renderFrameIndex として控えてから m_frameIndex を 1 つ進め、
		// 　 進めた後の番号向けの更新ジョブを Submit する ➡ 同じ周の中で「描画が読む番号」と「更新が書く番号」が
		// 　 1 つずれる。
		const uint64_t renderFrameIndex = m_frameIndex;
		++m_frameIndex;

		UpdateJobArguments arguments{};
		arguments.pipeline         = this;
		arguments.frameAllocator   = &m_frameMemory->GetCurrent();
		arguments.frameIndex       = m_frameIndex;
		arguments.deltaTimeSeconds = deltaTimeSeconds;
		arguments.gamepad          = gamepad;

		JobDesc desc{};
		desc.function     = &FramePipeline::RunUpdateJob;
		desc.arguments    = &arguments;
		desc.argumentSize = sizeof(arguments);

		// この後の描画は更新と関わらないので、投げるのは周のできるだけ早い位置にする。
		m_jobSystem->Submit(desc, &m_updateCounter);

#if FANG_ENABLE_PROFILER
		const auto renderBeginTime = std::chrono::steady_clock::now();
#endif

		// ③ 1 つ前のフレームを描く。
		// 　 GetSlotIndex(renderFrameIndex) は増分前の番号の面なので、直前の RunUpdate が書き終えた面を読む
		// 　 （更新ジョブが今書いている GetSlotIndex(m_frameIndex) とは別の面）。
		// 1 つ前のフレームを描く。更新が今書いている面とは別の面を読む。
		m_renderFunction(m_userData, m_frameData[GetSlotIndex(renderFrameIndex)], renderFrameIndex, deltaTimeSeconds);

#if FANG_ENABLE_PROFILER
		m_renderMilliseconds = GetElapsedMilliseconds(renderBeginTime);
#endif

		// ④ 更新ジョブの完了を待つ。
		// フレームメモリのアトミックは全部 relaxed なので、更新が書いた中身が見えるのはこの同期を通った後。
		// ➡ 周の末尾に置けば、ループを抜けた時点で投げっぱなしのジョブが無いことも一目で分かる。
		m_jobSystem->Wait(m_updateCounter);

#if FANG_ENABLE_PROFILER
		m_updateMilliseconds = m_updateMillisecondsSlots[GetSlotIndex(m_frameIndex)];
		m_frameMilliseconds  = deltaTimeSeconds * 1000.0f;
#endif
	}


	void FramePipeline::RunUpdateJob(void* arguments, uint32_t workerIndex)
	{
		FANG_UNUSED(workerIndex);

		const auto&    jobArguments = *static_cast<const UpdateJobArguments*>(arguments);
		FramePipeline& pipeline     = *jobArguments.pipeline;

		pipeline.RunUpdate(
			*jobArguments.frameAllocator,
			jobArguments.frameIndex,
			jobArguments.deltaTimeSeconds,
			jobArguments.gamepad
		);
	}


	void FramePipeline::RunUpdate(
		FrameAllocator&     frameAllocator,
		uint64_t            frameIndex,
		float               deltaTimeSeconds,
		const GamepadState& gamepad
	)
	{
		const FrameUpdateContext context{ frameAllocator, frameIndex, deltaTimeSeconds, gamepad };

#if FANG_ENABLE_PROFILER
		const auto updateBeginTime = std::chrono::steady_clock::now();
#endif

		m_frameData[GetSlotIndex(frameIndex)] = m_updateFunction(m_userData, context);

#if FANG_ENABLE_PROFILER
		m_updateMillisecondsSlots[GetSlotIndex(frameIndex)] = GetElapsedMilliseconds(updateBeginTime);
#endif
	}
} // namespace fang
