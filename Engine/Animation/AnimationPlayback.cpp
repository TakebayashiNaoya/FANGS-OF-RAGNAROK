/**
 * @file AnimationPlayback.cpp
 * @brief クリップの再生位置の実装。
 */
#include "Pch.h"
#include "Animation/AnimationPlayback.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/**
		 * @brief 値を 0 以上 length 未満に折り返す。
		 * @details `std::fmod` は負の値に対して負の余りを返すので、そのままだと後ろ向き再生で位置が
		 *          マイナスになる。1 周ぶん足して直す。
		 */
		[[nodiscard]] float WrapIntoRange(float value, float length)
		{
			const float remainder = std::fmodf(value, length);
			return remainder < 0.0f ? remainder + length : remainder;
		}
	} // namespace


	void AnimationPlayback::SetDurationSeconds(float durationSeconds)
	{
		m_durationSeconds = durationSeconds > 0.0f ? durationSeconds : 0.0f;

		if (m_durationSeconds <= 0.0f)
		{
			m_timeSeconds = 0.0f;
			return;
		}

		if (m_timeSeconds >= m_durationSeconds)
		{
			m_timeSeconds = WrapIntoRange(m_timeSeconds, m_durationSeconds);
		}
	}


	void AnimationPlayback::Advance(float deltaTimeSeconds)
	{
		// 尺が無いなら再生するものが無い。読み込みに失敗したときにここへ来る。
		if (m_durationSeconds <= 0.0f || deltaTimeSeconds <= 0.0f)
		{
			return;
		}

		m_timeSeconds = WrapIntoRange(m_timeSeconds + deltaTimeSeconds * m_playbackSpeed, m_durationSeconds);
	}


	float AnimationPlayback::GetTimeRatio() const
	{
		if (m_durationSeconds <= 0.0f)
		{
			return 0.0f;
		}

		// 折り返し済みなので 1.0 には届かないが、丸めで越えないよう最後に抑える。
		const float ratio = m_timeSeconds / m_durationSeconds;
		return ratio < 1.0f ? ratio : 1.0f;
	}
} // namespace fang
