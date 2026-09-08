/**
 * @file Log.h
 * @brief カテゴリ付きログ。
 */
#pragma once

#include <format>
#include <string_view>


namespace fang
{
	/** @brief ログの重大度。 */
	enum class EnLogLevel : uint8_t
	{
		Trace,
		Info,
		Warning,
		Error,
		Fatal,
	};

	/**
	 * @brief ログのカテゴリ。
	 * @details lib ごとに FANG_DEFINE_LOG_CATEGORY で 1 つ定義する。
	 */
	struct LogCategory
	{
		const char* name;
	};

	/**
	 * @brief 組み立て済みの 1 行を出力する。
	 * @details 呼ぶのは FANG_LOG_* マクロだけ。出力先はプラットフォームごとに違う（Windows はデバッガ出力と標準出力）。
	 * @threading 任意のスレッド。
	 */
	void WriteLog(const LogCategory& category, EnLogLevel level, std::string_view message);

	/** @brief ログを切った構成で引数を「使ったこと」にする。sizeof の中なので評価はされない。 */
	template <typename... Args> int IgnoreLogArguments(const Args&...);
} // namespace fang

/** @brief カテゴリを宣言する（ヘッダに書く）。 */
#define FANG_DECLARE_LOG_CATEGORY(categoryName) extern const fang::LogCategory FANG_LOG_CATEGORY_##categoryName

/** @brief カテゴリを定義する（.cpp に 1 か所だけ書く）。 */
#define FANG_DEFINE_LOG_CATEGORY(categoryName)                                                                         \
	const fang::LogCategory FANG_LOG_CATEGORY_##categoryName                                                           \
	{                                                                                                                  \
		#categoryName                                                                                                  \
	}

#if FANG_ENABLE_LOG
#define FANG_LOG(categoryName, level, ...)                                                                             \
	fang::WriteLog(FANG_LOG_CATEGORY_##categoryName, level, std::format(__VA_ARGS__))
#else
// Release でも FANG_FATAL だけは残す。
// 引数は sizeof の中で触るだけにする。評価はされないが「使っていない」警告も出ない。
#define FANG_LOG(categoryName, level, ...) ((void)sizeof(fang::IgnoreLogArguments(__VA_ARGS__)))
#endif

#define FANG_LOG_TRACE(categoryName, ...) FANG_LOG(categoryName, fang::EnLogLevel::Trace, __VA_ARGS__)
#define FANG_LOG_INFO(categoryName, ...) FANG_LOG(categoryName, fang::EnLogLevel::Info, __VA_ARGS__)
#define FANG_LOG_WARNING(categoryName, ...) FANG_LOG(categoryName, fang::EnLogLevel::Warning, __VA_ARGS__)
#define FANG_LOG_ERROR(categoryName, ...) FANG_LOG(categoryName, fang::EnLogLevel::Error, __VA_ARGS__)
