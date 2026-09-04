/**
 * @file MemoryUsage.h
 * @brief アプリが今どれだけメモリを使っているかを OS に聞く口。
 */
#pragma once

#include <cstdint>


namespace fang
{
	/** @brief OS が数えたメモリの使用量。 */
	struct MemoryUsage
	{
		uint64_t usedBytes  = 0; /**< アプリが使っているバイト数。測れなければ 0。 */
		uint64_t limitBytes = 0; /**< OS が申告する上限。分からなければ 0。 */
	};

	/**
	 * @brief 今のメモリ使用量を返す。
	 * @details Xbox は Game 分類の割り当てをそのまま返すので、上限は 5GB 前後になる。
	 *          Windows はプロセスのプライベート使用量で、上限は分からないので 0 を返す。
	 *          どちらも GPU 側の確保は入らない。Xbox は同じメモリを共有するので、実機では
	 *          この値より実際の消費が大きい。
	 * @return 測れなければ usedBytes が 0。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] MemoryUsage GetProcessMemoryUsage();
} // namespace fang
