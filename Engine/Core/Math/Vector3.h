/**
 * @file Vector3.h
 * @brief 3 成分のベクトル。
 */
#pragma once


namespace fang
{
	/**
	 * @brief 3 成分のベクトル。位置にも方向にも使う。
	 * @details 座標系は左手系 Y-up で、位置の単位は 1 = 1cm。
	 */
	struct Vector3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};
} // namespace fang
