/**
 * @file GameMain.h
 * @brief Win32 / UWP のどちらのエントリポイントからも通る、ゲームの起動処理。
 */
#pragma once

namespace fang::game
{
	/**
	 * @brief ゲームを起動し、終了コードを返す。
	 * @return プロセスの終了コード。
	 * @threading メインスレッドのみ。
	 */
	int Run();
} // namespace fang::game
