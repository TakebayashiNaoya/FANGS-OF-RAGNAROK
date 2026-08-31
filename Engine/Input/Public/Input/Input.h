/**
 * @file Input.h
 * @brief Input モジュールの入口（パッド / キーボード）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details Phase 1 の骨格。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetInputModuleName();
} // namespace fang
