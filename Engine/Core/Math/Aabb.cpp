/**
 * @file Aabb.cpp
 * @brief 境界ボックスの拡張と、行列による変換。
 */
#include "Pch.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 箱の中心。 */
		Vector3 GetCenter(const Aabb& bounds)
		{
			return (bounds.min + bounds.max) * 0.5f;
		}


		/** @brief 中心から各面までの距離。 */
		Vector3 GetExtent(const Aabb& bounds)
		{
			return (bounds.max - bounds.min) * 0.5f;
		}


		/**
		 * @brief 回転・拡縮の成分を絶対値にした行列を返す。
		 * @details 半径ベクトルは向きを持たないので、符号を落とした行列で回すと必ず包む長さになる。
		 */
		Matrix4x4 MakeAbsoluteMatrix(const Matrix4x4& matrix)
		{
			Matrix4x4 result;
			for (int row = 0; row < 3; ++row)
			{
				for (int column = 0; column < 3; ++column)
				{
					result.m[row][column] = std::abs(matrix.m[row][column]);
				}
			}
			return result;
		}
	} // namespace


	void Aabb::Expand(const Vector3& point)
	{
		min.x = (point.x < min.x) ? point.x : min.x;
		min.y = (point.y < min.y) ? point.y : min.y;
		min.z = (point.z < min.z) ? point.z : min.z;

		max.x = (point.x > max.x) ? point.x : max.x;
		max.y = (point.y > max.y) ? point.y : max.y;
		max.z = (point.z > max.z) ? point.z : max.z;
	}


	Aabb TransformAabb(const Aabb& bounds, const Matrix4x4& matrix)
	{
		FANG_ASSERT(bounds.IsValid(), "無効な箱を変換しようとしている");

		const Vector3 transformedCenter = TransformPoint(GetCenter(bounds), matrix);
		const Vector3 transformedExtent = TransformDirection(GetExtent(bounds), MakeAbsoluteMatrix(matrix));

		Aabb result;
		result.min = transformedCenter - transformedExtent;
		result.max = transformedCenter + transformedExtent;
		return result;
	}
} // namespace fang
