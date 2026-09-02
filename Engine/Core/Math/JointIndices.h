/**
 * @file JointIndices.h
 * @brief 頂点 1 つが影響を受ける関節の番号。
 */
#pragma once

#include <cstdint>


namespace fang
{
	/**
	 * @brief 頂点 1 つに紐づく関節の番号 4 つ組。
	 * @details glTF の JOINTS_0 と同じ形。重み（Vector4）と添字を合わせて使う。
	 *          Vector2 / Vector3 / Vector4 と同じく、読む側（Renderer）と書く側（Resource）の両方が
	 *          Core しか参照しなくて済むようにここへ置いている。
	 *          関節が 256 本を超えるモデルは扱わない。狼は 59 本。
	 */
	struct JointIndices
	{
		uint8_t joints[4] = {};
	};
} // namespace fang
