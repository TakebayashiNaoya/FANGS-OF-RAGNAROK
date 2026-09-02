/**
 * @file UI.h
 * @brief UI モジュールの入口（HUD / メニュー）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetUIModuleName();
} // namespace fang
