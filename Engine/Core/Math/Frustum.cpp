/**
 * @file Frustum.cpp
 * @brief ビュー射影行列からの平面抽出と、境界ボックスとの交差判定。
 */
#include "Pch.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/**
		 * @brief 行列の列を 1 本取り出す。
		 * @details 行ベクトル規約（p * M）では、クリップ座標の 1 成分が 1 本の列との内積になる。
		 *          ➡列そのものが「x とは何か」「w とは何か」を表す平面の材料になる。
		 */
		Vector4 GetColumn(const Matrix4x4& matrix, int columnIndex)
		{
			return Vector4{
				matrix.m[0][columnIndex],
				matrix.m[1][columnIndex],
				matrix.m[2][columnIndex],
				matrix.m[3][columnIndex],
			};
		}


		/** @brief 平面の係数どうしを足す。 */
		Vector4 Add(const Vector4& left, const Vector4& right)
		{
			return Vector4{ left.x + right.x, left.y + right.y, left.z + right.z, left.w + right.w };
		}


		/** @brief 平面の係数どうしを引く。 */
		Vector4 Subtract(const Vector4& left, const Vector4& right)
		{
			return Vector4{ left.x - right.x, left.y - right.y, left.z - right.z, left.w - right.w };
		}


		/**
		 * @brief 法線の長さが 1 になるように平面の係数を割る。
		 * @details 正規化しておくと w が原点からの実距離になり、交差判定を素の内積 1 本で書ける。
		 */
		Vector4 NormalizePlane(const Vector4& plane)
		{
			const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
			FANG_ASSERT(length > 0.0f, "平面の法線が 0 ベクトル。ビュー射影行列が壊れている");

			const float inverseLength = 1.0f / length;
			return Vector4{
				plane.x * inverseLength,
				plane.y * inverseLength,
				plane.z * inverseLength,
				plane.w * inverseLength,
			};
		}
	} // namespace


	void Frustum::ExtractFromViewProjection(const Matrix4x4& viewProjection)
	{
		const Vector4 columnX = GetColumn(viewProjection, 0);
		const Vector4 columnY = GetColumn(viewProjection, 1);
		const Vector4 columnZ = GetColumn(viewProjection, 2);
		const Vector4 columnW = GetColumn(viewProjection, 3);

		// 視錐台の中は -w <= x <= w、-w <= y <= w、0 <= z <= w。
		// 各不等式を「0 以上」の形へ移すと、係数がそのまま内向きの平面になる。
		planes[0] = NormalizePlane(Add(columnW, columnX));      // 左。x + w >= 0
		planes[1] = NormalizePlane(Subtract(columnW, columnX)); // 右。w - x >= 0
		planes[2] = NormalizePlane(Add(columnW, columnY));      // 下。y + w >= 0
		planes[3] = NormalizePlane(Subtract(columnW, columnY)); // 上。w - y >= 0
		planes[4] = NormalizePlane(columnZ);                    // 近。深度が [0, 1] なので w を足さない
		planes[5] = NormalizePlane(Subtract(columnW, columnZ)); // 遠。w - z >= 0
	}


	bool Frustum::Intersects(const Aabb& bounds) const
	{
		FANG_ASSERT(bounds.IsValid(), "無効な箱を視錐台と判定しようとしている");

		for (const Vector4& plane : planes)
		{
			// 法線の向きへ最も進んだ角だけを見る。そこが平面の裏なら、箱は丸ごと裏側にある。
			const Vector3 positiveVertex{
				(plane.x >= 0.0f) ? bounds.max.x : bounds.min.x,
				(plane.y >= 0.0f) ? bounds.max.y : bounds.min.y,
				(plane.z >= 0.0f) ? bounds.max.z : bounds.min.z,
			};

			const float distance =
				plane.x * positiveVertex.x + plane.y * positiveVertex.y + plane.z * positiveVertex.z + plane.w;
			if (distance < 0.0f)
			{
				return false;
			}
		}

		return true;
	}
} // namespace fang
