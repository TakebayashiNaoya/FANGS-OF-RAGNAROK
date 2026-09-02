/**
 * @file Audio.h
 * @brief Audio モジュールの入口（XAudio2）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetAudioModuleName();
} // namespace fang
