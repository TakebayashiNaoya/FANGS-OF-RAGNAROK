/**
 * @file EngineContext.h
 * @brief 上の層へ渡すエンジンの持ち物。
 */
#pragma once

namespace fang::rhi
{
	struct ShaderReloadStatus;
} // namespace fang::rhi


namespace fang
{
	class FrameMemory;
	class FramePipeline;
	class JobSystem;
	class PlatformBudget;

	/**
	 * @brief RunApplication が持っているものを、上の層から使えるようにまとめた束。
	 * @details 参照で持つのは、渡す側が起動から終了まで生かし続けると決まっていて、null を確かめる分岐が
	 *          要らないため。後から増えるものは、ここへメンバを 1 行足す。
	 */
	struct EngineContext
	{
		JobSystem&   jobSystem;
		FrameMemory& frameMemory;

		/** @brief 1 周の並びと所要時間。上の層は数字を読むだけなので const で渡す。 */
		const FramePipeline& framePipeline;

		/** @brief Xbox の予算に対する現在値。エディタから倍率と制限の入切を触るので const にしない。 */
		PlatformBudget& platformBudget;

#if FANG_ENABLE_HOT_RELOAD
		/** @brief 直近のシェーダーの作り直しの結果。GraphicsDevice が持っているものを借りる。 */
		const rhi::ShaderReloadStatus* shaderReloadStatus = nullptr;
#endif
	};
} // namespace fang
