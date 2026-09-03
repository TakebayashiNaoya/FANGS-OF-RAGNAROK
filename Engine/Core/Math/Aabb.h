/**
 * @file Aabb.h
 * @brief 軸平行の境界ボックスと、行列で変換する関数。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Vector3.h"
#include <cfloat>


namespace fang
{
	struct Matrix4x4;

	/**
	 * @brief 軸平行の境界ボックス。モデル空間にもワールド空間にも使う。
	 * @details 既定値は min が max より大きい「無効」な箱。➡点を Expand で入れていけば、最初の 1 点で
	 *          そのまま正しい箱になり、空かどうかの旗を別に持たなくて済む。境界を持たないものは
	 *          既定値のまま渡され、受け取った側が IsValid で見分ける。
	 */
	struct Aabb
	{
		/** @brief 箱として意味のある範囲を持っているか。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsValid() const
		{
			return min.x <= max.x && min.y <= max.y && min.z <= max.z;
		}

		/** @brief 点を含むように広げる。無効な箱に入れると、その点だけの箱になる。 */
		void Expand(const Vector3& point);

		Vector3 min{ FLT_MAX, FLT_MAX, FLT_MAX };    /**< 各軸の最小値。 */
		Vector3 max{ -FLT_MAX, -FLT_MAX, -FLT_MAX }; /**< 各軸の最大値。 */
	};

	/**
	 * @brief 箱を行列で変換し、変換後の形を包む新しい箱を作る。
	 * @param bounds 変換する箱。有効であること（IsValid）。
	 * @param matrix 行ベクトル規約の変換行列（p * M）。
	 * @return 変換後の 8 頂点をすべて含む軸平行の箱。回転が入ると元より大きくなる。
	 * @details 8 頂点を回さず、中心と半径ベクトルに分けて半径だけ絶対値の行列で回す。
	 *          回転の向きによらず必ず包む長さになるので、頂点の最大最小を取り直さなくてよい。
	 */
	[[nodiscard]] Aabb TransformAabb(const Aabb& bounds, const Matrix4x4& matrix);
} // namespace fang
