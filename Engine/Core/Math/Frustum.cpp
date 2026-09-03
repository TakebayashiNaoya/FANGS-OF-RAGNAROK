/**
 * @file Frustum.cpp
 * @brief ビュー射影行列からの平面抽出と、境界ボックスとの交差判定。
 */
#include "Pch.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"


namespace fang
{
	namespace
	{
		/**
		 * @brief 法線の長さが 1 になるように平面の係数を割る。
		 * @details 正規化しておくと w が原点からの実距離になり、交差判定を素の内積 1 本で書ける。
		 */
		Vector4 NormalizePlane(const Vector4& plane)
		{
			const Vector3 normal{ plane.x, plane.y, plane.z };
			const float   length = Length(normal);
			FANG_ASSERT(length > 0.0f, "平面の法線が 0 ベクトル。ビュー射影行列が壊れている");

			return plane * (1.0f / length);
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
		planes[0] = NormalizePlane(columnW + columnX); // 左。x + w >= 0
		planes[1] = NormalizePlane(columnW - columnX); // 右。w - x >= 0
		planes[2] = NormalizePlane(columnW + columnY); // 下。y + w >= 0
		planes[3] = NormalizePlane(columnW - columnY); // 上。w - y >= 0
		planes[4] = NormalizePlane(columnZ);           // 近。深度が [0, 1] なので w を足さない
		planes[5] = NormalizePlane(columnW - columnZ); // 遠。w - z >= 0
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

			const float distance = Dot(plane, Vector4{ positiveVertex.x, positiveVertex.y, positiveVertex.z, 1.0f });
			if (distance < 0.0f)
			{
				return false;
			}
		}

		return true;
	}
} // namespace fang
