/**
 * @file FramePipeline.h
 * @brief 更新をジョブへ投げ、その裏で 1 つ前のフレームを描く 1 周の並び。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Job/JobCounter.h"
#include "Runtime/FrameContext.h"
#include <cstddef>
#include <cstdint>


namespace fang
{
	class FrameMemory;
	class JobSystem;

	/**
	 * @brief 更新をジョブへ投げ、その裏で 1 つ前のフレームを描くループの中身。
	 * @details 更新が書いた中身が他のスレッドから見えるのは Wait を通った後なので、面の入れ替え ➡ Submit ➡
	 *          描画 ➡ Wait という並びをここ 1 か所に閉じ込める。RHI もウィンドウも知らないので、偽の 2 つの
	 *          関数を渡せばそのままテストから回せる。
	 * @threading メインスレッドのみ。Initialize で受け取る更新の関数だけがワーカースレッドで走る。
	 */
	class FramePipeline
	{
	public:
		FANG_NON_COPYABLE(FramePipeline);
		FANG_NON_MOVABLE(FramePipeline);

		/** @brief 更新の本体。このフレームの成果物を 1 つ返す。 */
		using UpdateFunction = FrameData* (*)(void* userData, const FrameUpdateContext& context);

		/** @brief 描画の本体。RHI に触るのはこの中だけ。 */
		using RenderFunction =
			void (*)(void* userData, const FrameData* frameData, uint64_t frameIndex, float deltaTimeSeconds);

		FramePipeline() = default;

		/** @brief 今の周で更新しているフレームの番号。描画はその 1 つ前を描く。 */
		[[nodiscard]] FANG_FORCEINLINE uint64_t GetFrameIndex() const { return m_frameIndex; }

		/** @brief 投げた更新ジョブを回収し終えているか。RunFrame の外では常に真。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsUpdateComplete() const { return m_updateCounter.IsComplete(); }

#if FANG_ENABLE_PROFILER
		/** @brief 直前の更新ジョブが走っていた時間（ミリ秒）。 */
		[[nodiscard]] FANG_FORCEINLINE float GetUpdateMilliseconds() const { return m_updateMilliseconds; }

		/** @brief 直前の描画にかかった時間（ミリ秒）。 */
		[[nodiscard]] FANG_FORCEINLINE float GetRenderMilliseconds() const { return m_renderMilliseconds; }

		/** @brief 直前の 1 周の時間（ミリ秒）。更新 + 描画より短ければ、その差だけ重なっている。 */
		[[nodiscard]] FANG_FORCEINLINE float GetFrameMilliseconds() const { return m_frameMilliseconds; }
#endif


	public:
		/**
		 * @brief 使うものと、更新・描画の本体を控える。
		 * @param userData 2 つの関数へそのまま渡す。Shutdown まで生きていること。
		 * @return どちらかの関数が null なら false。
		 */
		[[nodiscard]] bool Initialize(
			JobSystem&     jobSystem,
			FrameMemory&   frameMemory,
			void*          userData,
			UpdateFunction updateFunction,
			RenderFunction renderFunction
		);

		/** @brief 控えたものを手放す。二重に呼んでも安全。 */
		void Shutdown();

		/** @brief 助走。フレーム 0 の更新だけを同期で走らせ、描く相手を作る。 */
		void Prime();

		/** @brief 1 周。面の切り替え ➡ 更新 N を投げる ➡ 描画 N−1 ➡ 更新の完了待ち。 */
		void RunFrame(float deltaTimeSeconds);


	private:
		static constexpr size_t SLOT_COUNT = 2; /**< 更新しているフレームと、描いている 1 つ前で 2 面。 */

		/** @brief 更新ジョブの入口。写された引数から呼ぶ相手を取り出す。 */
		static void RunUpdateJob(void* arguments, uint32_t workerIndex);

		/** @brief フレーム番号から触ってよい面を決める。更新と描画が同じ面に当たらないようにするため。 */
		[[nodiscard]] static FANG_FORCEINLINE size_t GetSlotIndex(uint64_t frameIndex)
		{
			return static_cast<size_t>(frameIndex % SLOT_COUNT);
		}

		/** @brief 更新を 1 回走らせ、成果物と所要時間をそのフレームの面へ置く。 */
		void RunUpdate(FrameAllocator& frameAllocator, uint64_t frameIndex, float deltaTimeSeconds);

		JobSystem*   m_jobSystem   = nullptr;
		FrameMemory* m_frameMemory = nullptr;

		void*          m_userData       = nullptr;
		UpdateFunction m_updateFunction = nullptr;
		RenderFunction m_renderFunction = nullptr;

		/** @brief 更新の成果物。描画は 1 つ前のフレームの面を読むので、走っている更新と重ならない。 */
		FrameData* m_frameData[SLOT_COUNT]{};

		JobCounter m_updateCounter; /**< 更新ジョブ 1 本ぶん。周の末尾で 0 に戻す。 */

		uint64_t m_frameIndex = 0;     /**< 今の周で更新しているフレーム。助走で 0 を走らせる。 */
		bool     m_isPrimed   = false; /**< 助走が済んだか。RunFrame は描く相手がいることを前提にする。 */

#if FANG_ENABLE_PROFILER
		/** @brief 更新ジョブが書く所要時間。読むのは Wait を通った後のメインだけ。 */
		float m_updateMillisecondsSlots[SLOT_COUNT]{};

		float m_updateMilliseconds = 0.0f;
		float m_renderMilliseconds = 0.0f;
		float m_frameMilliseconds  = 0.0f;
#endif
	};
} // namespace fang
