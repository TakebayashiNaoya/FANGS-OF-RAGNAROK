/**
 * @file Resource.h
 * @brief Resource モジュールの入口（アセット形式 / ロード / ハンドル / ホットリロード）。
 */
#pragma once

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details Phase 1 の骨格。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetResourceModuleName();
} // namespace fang
