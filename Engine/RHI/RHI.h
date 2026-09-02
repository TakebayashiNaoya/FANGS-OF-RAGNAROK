/**
 * @file RHI.h
 * @brief RHI モジュールの入口（DirectX 12 の薄い抽象）。
 * @details このモジュールの公開ヘッダをまとめて include する。
 *          .cpp からはこれ 1 本で足りる。ヘッダの中では個別の include か前方宣言を使う。
 */
#pragma once

#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHILog.h"
#include "RHI/RHITypes.h"
#include "RHI/ShaderCompiler.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetRHIModuleName();
} // namespace fang
