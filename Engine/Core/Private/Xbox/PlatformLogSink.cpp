/**
 * @file PlatformLogSink.cpp
 * @brief ログの出力先（Xbox / UWP）。
 */
#include "Pch.h"
#include "Log/PlatformLogSink.h"
#include <windows.h>
#include <string>


namespace fang
{
	void WriteLogToPlatform(std::string_view line)
	{
		// TODO: 実機にはデバッガが付かないので LocalState/startup.log に落とす。
		const std::string terminated(line);
		::OutputDebugStringA(terminated.c_str());
	}
} // namespace fang
