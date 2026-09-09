/**
 * @file PlatformLogSink.cpp
 * @brief ログの出力先（Windows）。デバッガ出力に流す。
 */
#include "Pch.h"
#include "Core/Log/PlatformLogSink.h"
#include <windows.h>
#include <string>


namespace fang
{
	void WriteLogToPlatform(std::string_view line)
	{
		// OutputDebugStringA は null 終端が要るので詰め直す。
		const std::string terminated(line);
		WriteLogToPlatform(terminated.c_str());
	}


	void WriteLogToPlatform(const char* line)
	{
		::OutputDebugStringA(line);
	}
} // namespace fang
