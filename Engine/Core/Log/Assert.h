/**
 * @file Assert.h
 * @brief アサートと致命的エラー。
 */
#pragma once

#include "Core/Log/Log.h"


namespace fang
{
	/**
	 * @brief アサート失敗を報告して停止する。
	 * @details 呼ぶのは FANG_ASSERT / FANG_VERIFY / FANG_FATAL だけ。
	 * @threading 任意のスレッド。
	 */
	[[noreturn]] void OnAssertFailed(
		const char*      expression,
		std::string_view message,
		const char*      fileName,
		int              lineNumber
	);
} // namespace fang

#if FANG_ENABLE_ASSERT

/** @brief 契約違反（プログラミングミス）を止める。Release では消える。 */
#define FANG_ASSERT(condition, ...)                                                                                    \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(condition))                                                                                              \
		{                                                                                                              \
			fang::OnAssertFailed(#condition, std::format(__VA_ARGS__), __FILE__, __LINE__);                            \
		}                                                                                                              \
	} while (false)

/** @brief 式は常に評価し、Debug / Preview では結果も確かめる。 */
#define FANG_VERIFY(expression) FANG_ASSERT(expression, "FANG_VERIFY に失敗")

#else

// 条件もメッセージも sizeof の中で触るだけにする。評価はされないが「使っていない」警告も出ない。
#define FANG_ASSERT(condition, ...) ((void)sizeof(condition), (void)sizeof(fang::IgnoreLogArguments(__VA_ARGS__)))
#define FANG_VERIFY(expression) ((void)(expression))

#endif

/** @brief 回復不能。全構成でログを吐いて終了する。 */
#define FANG_FATAL(...) fang::OnAssertFailed(nullptr, std::format(__VA_ARGS__), __FILE__, __LINE__)
