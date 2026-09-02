/**
 * @file Animation.h
 * @brief Animation モジュールの入口（クリップ評価 / ブレンド / ステートマシン）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetAnimationModuleName();
} // namespace fang
