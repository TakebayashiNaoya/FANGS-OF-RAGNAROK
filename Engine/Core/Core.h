/**
 * @file Core.h
 * @brief Core モジュールの入口（Platform / Memory / Log / Math / Container / Job / Profiler / Reflection）。
 * @details このモジュールの公開ヘッダをまとめて include する。
 *          .cpp からはこれ 1 本で足りる。ヘッダの中では個別の include か前方宣言を使う。
 */
#pragma once

#include "Core/CoreLog.h"
#include "Core/CoreMacros.h"
#include "Core/Job/JobCounter.h"
#include "Core/Job/JobSystem.h"
#include "Core/Job/ParallelFor.h"
#include "Core/Log/Assert.h"
#include "Core/Log/Log.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/UniquePtr.h"
#include "Core/Platform/SystemFont.h"
#include "Core/Platform/Thread.h"
#include "Core/Platform/Window.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetCoreModuleName();
} // namespace fang
