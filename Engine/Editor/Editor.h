/**
 * @file Editor.h
 * @brief Editor モジュールの入口（ImGui エディタ）。
 * @details このモジュールの公開ヘッダをまとめて include する。
 *          .cpp からはこれ 1 本で足りる。ヘッダの中では個別の include か前方宣言を使う。
 */
#pragma once

#include "Editor/EditorLog.h"
#include "Editor/EditorUI.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetEditorModuleName();
} // namespace fang
