/**
 * @file Vector4.h
 * @brief 4 成分のベクトル。
 */
#pragma once


namespace fang
{
	/**
	 * @brief 4 成分のベクトル。
	 * @details 位置や方向のほか、スキンウェイトのように 4 つ組で意味を持つ値にも使う。
	 *          既定値を 0 にしてあるのは、重みとして使うときに「効かない」が正しい初期値だから。
	 */
	struct Vector4
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;
	};
} // namespace fang
