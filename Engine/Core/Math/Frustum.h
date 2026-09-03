/**
 * @file Frustum.h
 * @brief 視錐台の 6 平面と、境界ボックスとの交差判定。
 */
#pragma once

#include "Core/Math/Vector4.h"


namespace fang
{
	struct Aabb;
	struct Matrix4x4;

	/**
	 * @brief 視錐台を囲む 6 枚の平面。
	 * @details 平面は ax + by + cz + d = 0 の係数を Vector4 に詰めた形で、法線は視錐台の内側を向く。
	 *          ➡どの面でも「符号付き距離が負なら外」の 1 つの式で判定できる。
	 *          既定値は全成分 0 で、まだ何も抽出していない状態。抽出せずに判定を呼ばないこと。
	 */
	struct Frustum
	{
		/** @brief 平面の枚数。 */
		static constexpr int PLANE_COUNT = 6;

		/**
		 * @brief ビュー射影行列から 6 平面を取り出す。
		 * @param viewProjection 行ベクトル規約（p * World * ViewProjection）の合成行列。
		 * @details 深度が [0, 1] の D3D 流のクリップ空間を前提にする。[-1, 1] の行列を渡すと近平面がずれる。
		 *          法線は長さ 1 に正規化するので、w はそのまま原点からの符号付き距離になる。
		 */
		void ExtractFromViewProjection(const Matrix4x4& viewProjection);

		/**
		 * @brief 箱が視錐台と少しでも重なるか。
		 * @param bounds ワールド空間の箱。有効であること（IsValid）。無効な箱をどう扱うかは呼び出し側が決める。
		 * @return 完全に外側だと確かめられたときだけ false。角だけがかすっている場合も true を返す。
		 */
		[[nodiscard]] bool Intersects(const Aabb& bounds) const;

		/** @brief 左・右・下・上・近・遠の順。xyz が正規化した内向きの法線、w が原点からの符号付き距離。 */
		Vector4 planes[PLANE_COUNT];
	};
} // namespace fang
