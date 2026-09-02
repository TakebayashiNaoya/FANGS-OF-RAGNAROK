/**
 * @file Matrix4x4.h
 * @brief 4x4 行列と、ビュー行列・透視投影行列を作る関数。
 */
#pragma once

#include "Core/Math/Vector3.h"


namespace fang
{
	/**
	 * @brief 4x4 の行列。既定値は単位行列。
	 * @details 並びは行優先（m[行][列]）で、ベクトルは行として左から掛ける（p * M）。➡平行移動は m[3][0..2] に入り、
	 *          合成は効かせたい順に World * View * Projection と並べる。この 2 つの流儀を混ぜると、
	 *          転置した行列がそのまま通ってしまい原因の分からない歪みになる。
	 *          既定値を単位行列にしてあるのは、World のように「置き換えなければ何も起きない」のが正しい使い方だから。
	 */
	struct Matrix4x4
	{
		float m[4][4] = {
			{ 1.0f, 0.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f },
		};
	};

	/**
	 * @brief ワールド座標をビュー座標へ移す行列を作る。
	 * @param eye    視点の位置。単位は cm。
	 * @param target 注視点の位置。eye と重なっていないこと。
	 * @param up     おおよその上方向。視線と平行でないこと。正規化していなくてよい。
	 * @return 左手系 Y-up のビュー行列。視線が +Z、上が +Y、右が +X になる。
	 */
	[[nodiscard]] Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);

	/**
	 * @brief ビュー座標をクリップ座標へ移す透視投影行列を作る。
	 * @param fieldOfViewYRadians 垂直方向の画角。ラジアン。0 より大きく pi 未満であること。
	 * @param aspect              横 / 縦の比。0 より大きいこと。
	 * @param nearZ               近平面までの距離。cm。0 より大きいこと。
	 * @param farZ                遠平面までの距離。cm。nearZ より大きいこと。
	 * @return 左手系の透視投影行列。深度は D3D に合わせて近平面が 0、遠平面が 1 になる。
	 */
	[[nodiscard]] Matrix4x4 MakePerspectiveMatrix(float fieldOfViewYRadians, float aspect, float nearZ, float farZ);

	/**
	 * @brief 2 つの行列を掛ける。
	 * @param left  先に効かせる変換。
	 * @param right 後に効かせる変換。
	 * @return left * right。行ベクトル規約なので、点は left の変換を受けてから right の変換を受ける。
	 */
	[[nodiscard]] Matrix4x4 Multiply(const Matrix4x4& left, const Matrix4x4& right);
} // namespace fang
