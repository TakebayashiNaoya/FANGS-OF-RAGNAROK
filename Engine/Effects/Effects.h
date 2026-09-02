/**
 * @file Effects.h
 * @brief Effects モジュールの入口（パーティクル）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetEffectsModuleName();
} // namespace fang
