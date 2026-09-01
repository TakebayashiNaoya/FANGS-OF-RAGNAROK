/**
 * @file UWPApplication.h
 * @brief UWP(CoreApplication)でゲームループを回す入口。
 */
#pragma once

#if FANG_PLATFORM_UWP

namespace fang
{
	/**
	 * @brief CoreApplication を起動し、UI スレッドの上で runGame を呼ぶ。
	 * @details UWP はループを OS 側(IFrameworkView::Run)が持つ決まりなので、その中から
	 *          ゲーム本体を呼び返す。Window::Initialize はこの runGame の中でだけ使える。
	 * @param runGame ゲーム本体。CoreWindow が使える状態になってから呼ばれる。
	 * @return runGame の戻り値。起動に失敗したら 0 以外。
	 * @threading メインスレッドのみ。
	 */
	int RunUWPApplication(int (*runGame)());
} // namespace fang

#endif
