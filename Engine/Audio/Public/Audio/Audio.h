/**
 * @file Audio.h
 * @brief Audio モジュールの入口（XAudio2）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details Phase 1 の骨格。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetAudioModuleName();
} // namespace fang
