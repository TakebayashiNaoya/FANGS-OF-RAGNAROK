/**
 * @file Runtime.h
 * @brief Runtime モジュールの入口（App / フレームループ / モジュール初期化順）。
 * @details このモジュールの公開ヘッダをまとめて include する。
 *          .cpp からはこれ 1 本で足りる。ヘッダの中では個別の include か前方宣言を使う。
 */
#pragma once

#include "Runtime/Application.h"
#include "Runtime/EngineContext.h"
#include "Runtime/RuntimeLog.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details Phase 1 の骨格。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetRuntimeModuleName();
} // namespace fang
