/**
 * @file CpuProfiler.cpp
 * @brief CPUの処理時間を計測するクラスの実装
 */
#include "CpuProfiler.h"


namespace fangEngine::render
{
#ifdef FANG_DEBUG_OR_PROFILE

	void CpuProfiler::BeginFrame()
	{
		m_frameStartTime = std::chrono::steady_clock::now();
	}


	void CpuProfiler::EndFrame()
	{
		auto frameEndTime = std::chrono::steady_clock::now();
		m_frameMilliseconds = std::chrono::duration<float, std::milli>(frameEndTime - m_frameStartTime).count();
	}

#else

	// Releaseでは計測しないため、何もしない関数を用意する
	void CpuProfiler::BeginFrame() {}
	void CpuProfiler::EndFrame() {}

#endif // FANG_DEBUG_OR_PROFILE
} // namespace fangEngine::render
