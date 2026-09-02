/**
 * @file Thread.h
 * @brief スレッドまわりの OS 依存部分。
 */
#pragma once

#include <cstdint>


namespace fang
{
	/**
	 * @brief 実行に使えるコアの数を返す。ジョブのワーカー数はこれを元に決める。
	 * @details Windows は物理コア数（ハイパースレッディングの論理プロセッサ数ではない）。Xbox は
	 *          アプリに割り当てられる 6（OS が申告するのはハード全体の 8 で、使える数ではないため）。
	 * @return 1 以上。数えられなかったら 1。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] uint32_t GetUsableCoreCount();

	/**
	 * @brief 今のスレッドにデバッガ・プロファイラ用の名前を付ける。
	 * @param name UTF-8。31 文字を超える分は捨てる。
	 * @threading 名前を付けたいスレッド自身から呼ぶ。
	 */
	void SetCurrentThreadName(const char* name);
} // namespace fang
