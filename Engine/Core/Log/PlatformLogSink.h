/**
 * @file PlatformLogSink.h
 * @brief ログの OS 側の出力先。
 */
#pragma once

#include <string_view>


namespace fang
{
	/**
	 * @brief 組み立て済みの 1 行を OS に流す。
	 * @details Windows はデバッガ出力、Xbox（UWP）は LocalState のファイル。実装は Windows/ と Xbox/ にある。
	 * @threading 任意のスレッド。
	 */
	void WriteLogToPlatform(std::string_view line);

	/**
	 * @brief null 終端済みの 1 行を OS に流す。
	 * @details string_view を受ける形は終端のために std::string へ詰め直すので、その場で確保する。
	 *          メモリの錠を持ったまま報告を書く場所があり、そこから確保すると自分の錠で行き詰まる。
	 *          ➡確保しない口を別に用意する。
	 * @threading 任意のスレッド。
	 */
	void WriteLogToPlatform(const char* line);
} // namespace fang
