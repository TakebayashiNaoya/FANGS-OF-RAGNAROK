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
			return Vector3{
				(bounds.min.x + bounds.max.x) * 0.5f,
				(bounds.min.y + bounds.max.y) * 0.5f,
				(bounds.min.z + bounds.max.z) * 0.5f,
			};
		}


		/** @brief 中心から各面までの距離。 */
		Vector3 GetExtent(const Aabb& bounds)
		{
			return Vector3{
				(bounds.max.x - bounds.min.x) * 0.5f,
				(bounds.max.y - bounds.min.y) * 0.5f,
				(bounds.max.z - bounds.min.z) * 0.5f,
			};
		}


		/** @brief 方向を行ベクトルとして変換する。平行移動は掛けない。 */
		Vector3 TransformDirection(const Vector3& direction, const Matrix4x4& matrix)
		{
			return Vector3{
				direction.x * matrix.m[0][0] + direction.y * matrix.m[1][0] + direction.z * matrix.m[2][0],
				direction.x * matrix.m[0][1] + direction.y * matrix.m[1][1] + direction.z * matrix.m[2][1],
				direction.x * matrix.m[0][2] + direction.y * matrix.m[1][2] + direction.z * matrix.m[2][2],
			};
		}


		/** @brief 点を行ベクトルとして変換する（p * M）。平行移動は最終行にある。 */
		Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix)
		{
			const Vector3 rotated = TransformDirection(point, matrix);
			return Vector3{ rotated.x + matrix.m[3][0], rotated.y + matrix.m[3][1], rotated.z + matrix.m[3][2] };
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
		result.min = Vector3{
			transformedCenter.x - transformedExtent.x,
			transformedCenter.y - transformedExtent.y,
			transformedCenter.z - transformedExtent.z,
		};
		result.max = Vector3{
			transformedCenter.x + transformedExtent.x,
			transformedCenter.y + transformedExtent.y,
			transformedCenter.z + transformedExtent.z,
		};
		return result;
	}
} // namespace fang
