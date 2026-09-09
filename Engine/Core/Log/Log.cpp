/**
 * @file Log.cpp
 * @brief カテゴリ付きログの実装。
 */
#include "Pch.h"
#include "Core/Log/Log.h"
#include "Core/CoreLog.h"
#include "Core/Log/PlatformLogSink.h"
#include <cstdio>


FANG_DEFINE_LOG_CATEGORY(Core);


namespace fang
{
	namespace
	{
		const char* ToDisplayName(EnLogLevel level)
		{
			switch (level)
			{
				case EnLogLevel::Trace: return "Trace";
				case EnLogLevel::Info: return "Info";
				case EnLogLevel::Warning: return "Warning";
				case EnLogLevel::Error: return "Error";
				case EnLogLevel::Fatal: return "Fatal";
			}

			return "Unknown";
		}
	} // namespace


	void WriteLog(const LogCategory& category, EnLogLevel level, std::string_view message)
	{
		// TODO: ジョブから呼べるようロックフリーのリングバッファに積む。
		const std::string line = std::format("[{}][{}] {}\n", category.name, ToDisplayName(level), message);

		std::fputs(line.c_str(), level >= EnLogLevel::Error ? stderr : stdout);
		WriteLogToPlatform(line);
	}
} // namespace fang
