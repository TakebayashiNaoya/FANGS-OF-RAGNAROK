/**
 * @file Vector3.h
 * @brief 3 成分のベクトル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Log/Assert.h"
#include <cmath>


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

	/** @brief 成分どうしを足す。 */
	[[nodiscard]] FANG_FORCEINLINE Vector3 operator+(const Vector3& left, const Vector3& right)
	{
		return Vector3{ left.x + right.x, left.y + right.y, left.z + right.z };
	}

	/** @brief 成分どうしを引く。 */
	[[nodiscard]] FANG_FORCEINLINE Vector3 operator-(const Vector3& left, const Vector3& right)
	{
		return Vector3{ left.x - right.x, left.y - right.y, left.z - right.z };
	}

	/** @brief 向きを反転する。 */
	[[nodiscard]] FANG_FORCEINLINE Vector3 operator-(const Vector3& value)
	{
		return Vector3{ -value.x, -value.y, -value.z };
	}

	/** @brief スカラー倍。 */
	[[nodiscard]] FANG_FORCEINLINE Vector3 operator*(const Vector3& value, float scalar)
	{
		return Vector3{ value.x * scalar, value.y * scalar, value.z * scalar };
	}

	/** @brief スカラー倍。 */
	[[nodiscard]] FANG_FORCEINLINE Vector3 operator*(float scalar, const Vector3& value)
	{
		return value * scalar;
	}

	/** @brief 加算して代入する。 */
	FANG_FORCEINLINE Vector3& operator+=(Vector3& left, const Vector3& right)
	{
		left = left + right;
		return left;
	}

	/** @brief 減算して代入する。 */
	FANG_FORCEINLINE Vector3& operator-=(Vector3& left, const Vector3& right)
	{
		left = left - right;
		return left;
	}

	/**
	 * @brief 成分を番号で読む。0 = x、1 = y、2 = z。
	 * @details 3 軸を同じ式で回す計算（軸ごとの clamp、分離軸の投影）のためにある。範囲外は FANG_ASSERT。
	 */
	[[nodiscard]] FANG_FORCEINLINE float GetComponent(const Vector3& value, int index)
	{
		FANG_ASSERT(index >= 0 && index < 3, "成分の番号が範囲外");

		return (index == 0) ? value.x : ((index == 1) ? value.y : value.z);
	}

	/** @brief 成分を番号で書く。0 = x、1 = y、2 = z。 */
	FANG_FORCEINLINE void SetComponent(Vector3* target, int index, float value)
	{
		FANG_ASSERT(index >= 0 && index < 3, "成分の番号が範囲外");

		if (index == 0)
		{
			target->x = value;
		}
		else if (index == 1)
		{
			target->y = value;
		}
		else
		{
			target->z = value;
		}
	}

	/** @brief 内積。 */
	[[nodiscard]] FANG_FORCEINLINE float Dot(const Vector3& left, const Vector3& right)
	{
		return left.x * right.x + left.y * right.y + left.z * right.z;
	}

	/** @brief 外積。 */
	[[nodiscard]] FANG_FORCEINLINE Vector3 Cross(const Vector3& left, const Vector3& right)
	{
		return Vector3{
			left.y * right.z - left.z * right.y,
			left.z * right.x - left.x * right.z,
			left.x * right.y - left.y * right.x,
		};
	}

	/** @brief 長さの 2 乗。長さそのものが要らない比較では、これで平方根を省ける。 */
	[[nodiscard]] FANG_FORCEINLINE float LengthSquared(const Vector3& value)
	{
		return Dot(value, value);
	}

	/** @brief 長さ。 */
	[[nodiscard]] FANG_FORCEINLINE float Length(const Vector3& value)
	{
		return std::sqrt(LengthSquared(value));
	}

	/**
	 * @brief 長さ 1 の向きに正規化する。
	 * @details 0 ベクトルを渡すと長さで割れないので呼ばないこと。
	 */
	[[nodiscard]] inline Vector3 Normalize(const Vector3& value)
	{
		const float length = Length(value);
		FANG_ASSERT(length > 0.0f, "長さ 0 のベクトルを正規化しようとしている");

		return value * (1.0f / length);
	}
} // namespace fang
