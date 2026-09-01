/**
 * @file CoreMacros.h
 * @brief どのモジュールからも使う基本マクロ。
 */
#pragma once

/** @brief 強制インライン。プラットフォーム差分を Core に閉じ込めるため __forceinline は直接書かない。 */
#define FANG_FORCEINLINE __forceinline

/** @brief コピーを禁止する。クラス定義の先頭に書く。 */
#define FANG_NON_COPYABLE(ClassName)                                                                                   \
	ClassName(const ClassName&)            = delete;                                                                   \
	ClassName& operator=(const ClassName&) = delete

/** @brief ムーブも含めて禁止する。 */
#define FANG_NON_MOVABLE(ClassName)                                                                                    \
	ClassName(ClassName&&)            = delete;                                                                        \
	ClassName& operator=(ClassName&&) = delete

/** @brief 未使用の引数を明示的に捨てる。 */
#define FANG_UNUSED(value) ((void)(value))

/** @brief 配列の要素数。 */
#define FANG_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))
