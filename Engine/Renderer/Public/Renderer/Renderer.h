/**
 * @file Renderer.h
 * @brief Renderer モジュールの入口（RenderGraph / メッシュ / マテリアル / シャドウ / DebugDraw）。
 * @details このモジュールの公開ヘッダをまとめて include する。
 *          .cpp からはこれ 1 本で足りる。ヘッダの中では個別の include か前方宣言を使う。
 */
#pragma once

#include "Renderer/RendererLog.h"
#include "Renderer/TriangleRenderer.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details Phase 1 の骨格。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetRendererModuleName();
} // namespace fang
