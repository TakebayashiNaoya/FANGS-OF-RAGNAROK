/**
 * @file AI.h
 * @brief AI モジュールの入口（FSM / ビヘイビアツリー / 感知）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetAIModuleName();
} // namespace fang
