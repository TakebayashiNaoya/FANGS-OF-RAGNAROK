/**
 * @file CpuProfiler.h
 * @brief CPUの処理時間を計測するクラス
 */
#pragma once
#include <chrono>


namespace fangEngine::render
{
	/**
	 * @brief CPUの処理時間を計測するクラス
	 * @details BeginFrameからEndFrameまでのCPUの処理時間を測る。
	 *          構成の切り替えの判定をRenderContextに書かなくていいようにこのクラスを作った。
	 */
	class CpuProfiler
	{
	public:
		/**
		 * @brief 計測を始める
		 */
		void BeginFrame();

		/**
		 * @brief 計測を終わらせる
		 */
		void EndFrame();

		/**
		 * @brief 計測した時間をミリ秒単位で返す
		 * @return 計測した時間(ミリ秒)
		 */
		inline float GetFrameMilliseconds() const { return m_frameMilliseconds; }


	private:
		std::chrono::steady_clock::time_point m_frameStartTime;           // 待ちのあとでCPUの処理を始めた時刻
		float                                 m_frameMilliseconds = 0.0f; // 直前のフレームのCPU処理の時間
	};
} // namespace fangEngine::render
