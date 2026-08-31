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
	 * @details Windows はデバッガ出力、Xbox（UWP）は LocalState のファイル。実装は Private/Windows と Private/Xbox にある。
	 * @threading 任意のスレッド。
	 */
	void WriteLogToPlatform(std::string_view line);
} // namespace fang
