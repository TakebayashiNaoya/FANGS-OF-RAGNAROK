/**
 * @file Input.h
 * @brief Input モジュールの入口（パッド / キーボード）。
 * @details 公開ヘッダをまとめた傘ヘッダ。他モジュールの .cpp はこれ 1 本で足りる。
 *          ヘッダの中からは傘を使わず、個別の include か前方宣言にすること。
 */
#pragma once

#include "Input/Gamepad.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetInputModuleName();
} // namespace fang
