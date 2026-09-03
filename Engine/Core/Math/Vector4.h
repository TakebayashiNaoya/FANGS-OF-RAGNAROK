/**
 * @file Vector4.h
 * @brief 4 成分のベクトル。
 */
#pragma once

#include "Core/CoreMacros.h"


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

	/** @brief 成分どうしを足す。 */
	[[nodiscard]] FANG_FORCEINLINE Vector4 operator+(const Vector4& left, const Vector4& right)
	{
		return Vector4{ left.x + right.x, left.y + right.y, left.z + right.z, left.w + right.w };
	}

	/** @brief 成分どうしを引く。 */
	[[nodiscard]] FANG_FORCEINLINE Vector4 operator-(const Vector4& left, const Vector4& right)
	{
		return Vector4{ left.x - right.x, left.y - right.y, left.z - right.z, left.w - right.w };
	}

	/** @brief スカラー倍。 */
	[[nodiscard]] FANG_FORCEINLINE Vector4 operator*(const Vector4& value, float scalar)
	{
		return Vector4{ value.x * scalar, value.y * scalar, value.z * scalar, value.w * scalar };
	}

	/** @brief 内積。 */
	[[nodiscard]] FANG_FORCEINLINE float Dot(const Vector4& left, const Vector4& right)
	{
		return left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
	}
} // namespace fang
