/**
 * @file Aabb.h
 * @brief 軸平行の境界ボックスと、行列で変換する関数。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Vector3.h"
#include <cfloat>
#include <span>


namespace fang
{
	struct Matrix4x4;

	/**
	 * @brief 軸平行の境界ボックス。モデル空間にもワールド空間にも使う。
	 * @details min と max は対角の 2 角で、各軸の最小値・最大値をそれぞれ集めたもの。既定値は min に +FLT_MAX、
	 *          max に -FLT_MAX という逆さの値を入れた「無効」な箱 ➡ 最初の Expand で min = max = その点になり、
	 *          空かどうかの旗を別に持たずに済む（最大値探索を -∞ から始めるのと同じ理屈）。境界を持たないもの
	 *          は既定値のまま渡され、受け取った側が IsValid で見分ける。
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

		/**
		 * @brief 8 頂点を求める。
		 * @param outCorners 書き込み先。並びは (min/max の x, y, z) を 2 進数の桁と見た順
		 *                   （0 = min, 1 = max。x が最下位桁）で、0 番が min、7 番が max になる。
		 * @details 無効な箱で呼ぶと FANG_ASSERT。デバッグ描画や変換など、min/max だけでは
		 *          足りない場面のための導出関数。
		 */
		void GetCorners(Vector3 (&outCorners)[8]) const;

		Vector3 min{ FLT_MAX, FLT_MAX, FLT_MAX };    /**< 対角の 1 点。各軸の最小値（空の箱の既定は +FLT_MAX）。 */
		Vector3 max{ -FLT_MAX, -FLT_MAX, -FLT_MAX }; /**< 対角のもう 1 点。各軸の最大値（既定は -FLT_MAX）。 */
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

	/**
	 * @brief 点列をすべて含む箱を作る。
	 * @param points 元になる点列。
	 * @return points を全部 Expand した箱。points が空なら無効な箱（Aabb::IsValid() が false）。
	 */
	[[nodiscard]] Aabb MakeAabbFromPoints(std::span<const Vector3> points);
} // namespace fang
