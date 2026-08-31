/**
 * @file Assert.cpp
 * @brief アサートと致命的エラーの実装。
 */
#include "Pch.h"
#include "Core/Log/Assert.h"
#include "Core/CoreLog.h"
#include "Log/PlatformLogSink.h"
#include <cstdio>
#include <cstdlib>


namespace fang
{
	void OnAssertFailed(const char* expression, std::string_view message, const char* fileName, int lineNumber)
	{
		const std::string line =
			expression != nullptr
				? std::format("[Core][Fatal] {}({}): {} — {}\n", fileName, lineNumber, expression, message)
				: std::format("[Core][Fatal] {}({}): {}\n", fileName, lineNumber, message);

		// FANG_ENABLE_LOG に関係なく出す。
		std::fputs(line.c_str(), stderr);
		WriteLogToPlatform(line);

		std::abort();
	}
} // namespace fang
