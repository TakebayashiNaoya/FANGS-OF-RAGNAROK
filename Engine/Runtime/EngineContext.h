/**
 * @file EngineContext.h
 * @brief 上の層へ渡すエンジンの持ち物。
 */
#pragma once

namespace fang
{
	class JobSystem;

	/**
	 * @brief RunApplication が持っているものを、上の層から使えるようにまとめた束。
	 * @details 参照で持つのは、渡す側が起動から終了まで生かし続けると決まっていて、null を確かめる分岐が
	 *          要らないため。フレームアロケータのように後から増えるものは、ここへメンバを 1 行足す。
	 */
	struct EngineContext
	{
		JobSystem& jobSystem;
	};
} // namespace fang
