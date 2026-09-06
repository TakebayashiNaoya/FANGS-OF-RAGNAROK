/**
 * @file CameraFollowParameter.h
 * @brief 操作する狼を追うカメラの調整値。
 */
#pragma once

#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"


namespace fang::game
{
	/**
	 * @brief 狼を追うカメラの調整値。
	 * @details FANG_REFLECT 付きの POD（03 コーディング規約 14）。距離・俯角は狼 1 匹をちょうど収める値に
	 *          してある（根拠は distanceCentimeters の注記）。
	 */
	struct CameraFollowParameter
	{
		FANG_REFLECT_BEGIN(CameraFollowParameter)
		FANG_FIELD(distanceCentimeters, "距離", Range(0.0f, 2000.0f))
		FANG_FIELD(pitchRadians, "俯角", Range(0.0f, 1.5f))
		FANG_FIELD(fieldOfViewYRadians, "画角", Range(0.1f, 3.0f))
		FANG_FIELD(yawSpeedRadiansPerSecond, "右スティックの旋回速度", Range(0.0f, 8.0f))
		FANG_FIELD(orbitSecondsWhenDisconnected, "パッドが無いときの周回秒数", Range(1.0f, 120.0f))
		FANG_REFLECT_END()

		// 狼の頂点の実測範囲は X[-92.6, 111.4]（体長 204）、Y[-0.39, 106.6]、Z[±18.2]。1 匹を画面に収めたい
		// 半径は前後方向の張り出し 111cm。16:9 なので tan(横半角) = (16/9) * tan(30 度) = 1.026 ➡ 横半角 45.7 度。
		// 111 / sin(45.7 度) = 155cm に、動いても画面から出ない余白を足した値にしてある。
		float distanceCentimeters = 350.0f;

		// 水平のままだと視点が低く地表に潜りやすいので、見上げない程度に持ち上げる。
		float pitchRadians = 20.0f * PI / 180.0f;

		float fieldOfViewYRadians = 60.0f * PI / 180.0f;

		/** @brief 右スティックを倒し切ったときにカメラが回る速さ。 */
		float yawSpeedRadiansPerSecond = 2.5f;

		/** @brief パッドが繋がっていないときに自動で 1 周する秒数（起動して放置する確認手順のため）。 */
		float orbitSecondsWhenDisconnected = 20.0f;

		/** @brief 注視点に足すオフセット。足元ではなく胴の高さを見せる。FANG_FIELD にしていない固定値。 */
		Vector3 targetOffset{ 0.0f, 60.0f, 0.0f };
	};
} // namespace fang::game
