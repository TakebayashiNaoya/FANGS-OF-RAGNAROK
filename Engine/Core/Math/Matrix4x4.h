/**
 * @file Matrix4x4.h
 * @brief 4x4 行列と、ビュー行列・透視投影行列を作る関数。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"


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
	 * @brief ビュー座標をクリップ座標へ移す正射影行列を作る。
	 * @param left   視錐台の左端。cm。
	 * @param right  視錐台の右端。cm。left より大きいこと。
	 * @param bottom 視錐台の下端。cm。
	 * @param top    視錐台の上端。cm。bottom より大きいこと。
	 * @param nearZ  近平面までの距離。cm。
	 * @param farZ   遠平面までの距離。cm。nearZ より大きいこと。
	 * @return 左手系の正射影行列。深度は D3D に合わせて近平面が 0、遠平面が 1 になる。
	 */
	[[nodiscard]] Matrix4x4 MakeOrthographicOffCenterMatrix(
		float left,
		float right,
		float bottom,
		float top,
		float nearZ,
		float farZ
	);

	/**
	 * @brief 2 つの行列を掛ける。
	 * @param left  先に効かせる変換。
	 * @param right 後に効かせる変換。
	 * @return left * right。行ベクトル規約なので、点は left の変換を受けてから right の変換を受ける。
	 */
	[[nodiscard]] Matrix4x4 Multiply(const Matrix4x4& left, const Matrix4x4& right);

	/**
	 * @brief 右手系の行列を左手系へ移す。
	 * @param rightHanded 右手系のまま作られた行列。glTF と ozz-animation が返すものがこれ。
	 * @return 同じ動きを左手系で表す行列。
	 * @details 頂点は読み込みのときに Z を反転して左手系にしている（p_左 = p_右 S、S = diag(1, 1, -1, 1)）。
	 *          同じ座標に掛ける行列は S M S になり、行と列のどちらか片方だけが Z の成分だけ符号が返る。
	 *          頂点だけ直して行列を直さないと、骨に沿った動きが Z 方向に裏返る。
	 */
	[[nodiscard]] Matrix4x4 ConvertToLeftHanded(const Matrix4x4& rightHanded);

	/**
	 * @brief 点を行ベクトルとして変換する（p * M）。
	 * @param point  変換する位置。
	 * @param matrix 行ベクトル規約の変換行列。
	 * @return 変換後の位置。平行移動（m[3][0..2]）も足される。
	 */
	[[nodiscard]] Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix);

	/**
	 * @brief 向きを行ベクトルとして変換する（p * M）。
	 * @param direction 変換する向き。
	 * @param matrix    行ベクトル規約の変換行列。
	 * @return 変換後の向き。平行移動は掛からない。拡縮を含む行列を渡すと長さは保たれない。
	 */
	[[nodiscard]] Vector3 TransformDirection(const Vector3& direction, const Matrix4x4& matrix);

	/** @brief 行列の行を 1 本取り出す。rowIndex は 0〜3。 */
	[[nodiscard]] FANG_FORCEINLINE Vector4 GetRow(const Matrix4x4& matrix, int rowIndex)
	{
		return Vector4{ matrix.m[rowIndex][0], matrix.m[rowIndex][1], matrix.m[rowIndex][2], matrix.m[rowIndex][3] };
	}

	/** @brief 行列の列を 1 本取り出す。columnIndex は 0〜3。 */
	[[nodiscard]] FANG_FORCEINLINE Vector4 GetColumn(const Matrix4x4& matrix, int columnIndex)
	{
		return Vector4{
			matrix.m[0][columnIndex],
			matrix.m[1][columnIndex],
			matrix.m[2][columnIndex],
			matrix.m[3][columnIndex],
		};
	}
} // namespace fang
