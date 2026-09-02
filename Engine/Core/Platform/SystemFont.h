/**
 * @file SystemFont.h
 * @brief OS が持っているフォントの場所。
 */
#pragma once

#include <string>


namespace fang
{
	/**
	 * @brief UI に使える日本語フォントのパスを返す。
	 * @details 暫定。フォントは本来 Assets/Fonts/ に置いて AssetBuilder で SDF 化するので、
	 *          そこまでの間だけ OS のフォントを借りる。
	 * @return 見つかったパス。1 つも無ければ空文字列。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] std::string GetSystemUIFontPath();
} // namespace fang
