/**
 * @file Collision.h
 * @brief Collision モジュールの入口（Broadphase / Narrowphase / Query）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetCollisionModuleName();
} // namespace fang
