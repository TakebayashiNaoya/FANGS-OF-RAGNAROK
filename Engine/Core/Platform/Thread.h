/**
 * @file Thread.h
 * @brief スレッドまわりの OS 依存部分。
 */
#pragma once

#include <cstdint>


namespace fang
{
	/**
	 * @brief 物理コアの数を返す。
	 * @details ハイパースレッディングの論理プロセッサ数ではない。ジョブのワーカー数はこれを元に決める。
	 * @return 1 以上。数えられなかったら 1。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] uint32_t GetPhysicalCoreCount();

	/**
	 * @brief 今のスレッドにデバッガ・プロファイラ用の名前を付ける。
	 * @param name UTF-8。31 文字を超える分は捨てる。
	 * @threading 名前を付けたいスレッド自身から呼ぶ。
	 */
	void SetCurrentThreadName(const char* name);
} // namespace fang
