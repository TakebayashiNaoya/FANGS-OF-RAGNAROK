/**
 * @file WolfMovementParameter.h
 * @brief 狼の移動速度・旋回速度の調整値。
 */
#pragma once

#include "Core/Reflection/Reflection.h"


namespace fang::game
{
	/**
	 * @brief 狼 1 体ぶんの移動の調整値。
	 * @details FANG_REFLECT 付きの POD（03 コーディング規約 14）。JSON はまだ読めないので既定値はコード内。
	 */
	struct WolfMovementParameter
	{
		FANG_REFLECT_BEGIN(WolfMovementParameter)
		FANG_FIELD(moveSpeedCentimetersPerSecond, "移動速度", Range(0.0f, 2000.0f))
		FANG_FIELD(turnSpeedRadiansPerSecond, "旋回速度", Range(0.0f, 32.0f))
		FANG_REFLECT_END()

		// 体長 204cm に対して 400cm/秒 は「小走り」くらい。旋回 8 ラジアン/秒（1 周 0.8 秒）は
		// 真後ろへ倒しても 0.4 秒で振り向く速さ。
		// moveSpeedCentimetersPerSecond を上げるときは FrameClock::MAXIMUM_DELTA_TIME_SECONDS の
		// 導出（上限 × 最大速度 < 狼のカプセル半径）を見直すこと。
		float moveSpeedCentimetersPerSecond = 400.0f;
		float turnSpeedRadiansPerSecond     = 8.0f;
	};
} // namespace fang::game
