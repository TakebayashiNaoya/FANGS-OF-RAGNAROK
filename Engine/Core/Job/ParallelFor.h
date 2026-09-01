/**
 * @file ParallelFor.h
 * @brief 範囲を分けて並列に回す糖衣。
 */
#pragma once

#include "Core/Job/JobCounter.h"
#include "Core/Job/JobSystem.h"
#include "Core/Log/Assert.h"
#include <cstdint>
#include <type_traits>


namespace fang
{
	namespace detail
	{
		/** @brief ジョブ 1 件が受け持つ範囲と、そこで呼ぶ本体。 */
		template <typename TBody> struct ParallelForBatch
		{
			TBody    body;
			uint32_t begin;
			uint32_t end;
		};

		template <typename TBody> void RunParallelForBatch(void* arguments, uint32_t workerIndex)
		{
			const auto& batch = *static_cast<const ParallelForBatch<TBody>*>(arguments);
			for (uint32_t index = batch.begin; index < batch.end; ++index)
			{
				batch.body(index, workerIndex);
			}
		}
	} // namespace detail

	/**
	 * @brief [begin, end) を batchSize ずつに割ってジョブにし、全部終わるまで待つ。
	 * @param batchSize 1 ジョブが受け持つ要素数。細かすぎると積む手間のほうが高くつく。
	 * @param body      void(uint32_t index, uint32_t workerIndex)。ワーカーごとの配列を引くのに番号を使う。
	 *                  ジョブの引数へ写すので trivially copyable であること（参照キャプチャは可）。
	 * @details 呼んだスレッドも実行に参加する。begin >= end なら何もしない。
	 * @threading メインスレッドとジョブの中から呼べる。
	 */
	template <typename TBody>
	inline void ParallelFor(JobSystem& jobSystem, uint32_t begin, uint32_t end, uint32_t batchSize, const TBody& body)
	{
		static_assert(
			std::is_trivially_copyable_v<TBody>,
			"ParallelFor の本体はジョブの引数へ写すので trivially copyable であること"
		);
		static_assert(
			sizeof(detail::ParallelForBatch<TBody>) <= JobSystem::MAX_ARGUMENT_SIZE,
			"ParallelFor の本体が大きすぎる。掴むものを構造体にまとめてポインタで渡すこと"
		);

		FANG_ASSERT(batchSize > 0, "batchSize が 0 だとジョブが 1 件も進まない");

		if (begin >= end)
		{
			return;
		}

		JobCounter counter;

		JobDesc desc{};
		desc.function     = &detail::RunParallelForBatch<TBody>;
		desc.argumentSize = sizeof(detail::ParallelForBatch<TBody>);

		for (uint32_t batchBegin = begin; batchBegin < end; batchBegin += batchSize)
		{
			// 残りが batchSize より少ない最後の 1 件で end を越えないようにする。
			const uint32_t remaining = end - batchBegin;
			const uint32_t batchEnd  = batchBegin + (remaining < batchSize ? remaining : batchSize);

			const detail::ParallelForBatch<TBody> batch{ body, batchBegin, batchEnd };
			desc.arguments = &batch;
			jobSystem.Submit(desc, &counter);
		}

		jobSystem.Wait(counter);
	}
} // namespace fang
