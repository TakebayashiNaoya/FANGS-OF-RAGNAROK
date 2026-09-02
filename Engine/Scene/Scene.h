/**
 * @file Scene.h
 * @brief Scene モジュールの入口（GameObject から ECS へ / Transform 階層）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetSceneModuleName();
} // namespace fang
