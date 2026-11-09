/**
 * @file Matrix.h
 * @brief 4x4の行列の型と、その計算をまとめたファイル
 * @details 計算はDirectXMathで行う。座標は左手系。
 */
#pragma once
#include <DirectXMath.h>
#include "Core/Math/Quaternion.h"
#include "Core/Math/Vector.h"


namespace fangEngine::core
{
	/**
	 * @brief 4x4の行列
	 * @details DirectXMathと同じ行ベクトルの決まりで、ベクトルに左から掛ける(v * M)。
	 * そのため、a * bはaを先に、bを後に当てる行列になる(world * view * projection)。
	 * 保存はXMFLOAT4X4で持ち、計算のときだけXMMATRIXに載せる。
	 */
	struct Matrix
	{
		DirectX::XMFLOAT4X4 elements{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		}; // 行列の16個の数(最初は単位行列)

		static const Matrix IDENTITY; // 単位行列

		/**
		 * @brief 平行移動の行列を作る
		 * @param translation 動かす量
		 * @return 平行移動の行列
		 */
		static Matrix Translation(const Vector3& translation);

		/**
		 * @brief 拡大縮小の行列を作る
		 * @param scale 軸ごとの倍率
		 * @return 拡大縮小の行列
		 */
		static Matrix Scaling(const Vector3& scale);

		/**
		 * @brief X軸の周りに回す行列を作る
		 * @param radians 回す角度
		 * @return 回転の行列
		 */
		static Matrix RotationX(float radians);

		/**
		 * @brief Y軸の周りに回す行列を作る
		 * @param radians 回す角度(上から見て時計回りが正)
		 * @return 回転の行列
		 */
		static Matrix RotationY(float radians);

		/**
		 * @brief Z軸の周りに回す行列を作る
		 * @param radians 回す角度
		 * @return 回転の行列
		 */
		static Matrix RotationZ(float radians);

		/**
		 * @brief クォータニオンと同じ回転の行列を作る
		 * @param rotation 回転
		 * @return 回転の行列
		 */
		static Matrix Rotation(const Quaternion& rotation);

		/**
		 * @brief カメラの位置と見る点からビュー行列を作る
		 * @param eye カメラの位置
		 * @param target 見る点
		 * @param up カメラの上の向き
		 * @return ビュー行列
		 */
		static Matrix LookAt(const Vector3& eye, const Vector3& target, const Vector3& up);

		/**
		 * @brief 遠くの物ほど小さく映す射影行列を作る
		 * @param verticalFieldOfView 縦の画角
		 * @param aspectRatio 画面の横÷縦
		 * @param nearZ 映す範囲の手前の距離
		 * @param farZ 映す範囲の奥の距離
		 * @return 射影行列
		 */
		static Matrix Perspective(float verticalFieldOfView, float aspectRatio, float nearZ, float farZ);
	};

	inline constexpr Matrix Matrix::IDENTITY{};


	/**
	 * @brief DirectXMathで計算するために、レジスタへ載せる
	 * @param matrix 載せる行列
	 * @return レジスタの行列
	 */
	inline DirectX::XMMATRIX ToXMMATRIX(const Matrix& matrix) { return DirectX::XMLoadFloat4x4(&matrix.elements); }

	/**
	 * @brief DirectXMathで計算した行列を、レジスタから戻す
	 * @param matrix レジスタの行列
	 * @return 戻した行列
	 */
	inline Matrix ToMatrix(DirectX::FXMMATRIX matrix)
	{
		Matrix result{};
		DirectX::XMStoreFloat4x4(&result.elements, matrix);
		return result;
	}

	/**
	 * @brief 2つの行列を掛ける
	 * @param a 先に当てる行列
	 * @param b 後に当てる行列
	 * @return aを先に、bを後に当てる行列
	 */
	inline Matrix operator*(const Matrix& a, const Matrix& b) { return ToMatrix(DirectX::XMMatrixMultiply(ToXMMATRIX(a), ToXMMATRIX(b))); }

	inline Matrix& operator*=(Matrix& a, const Matrix& b) { return a = a * b; }

	/**
	 * @brief 逆行列を返す
	 * @param matrix 元の行列
	 * @return 逆行列(逆行列がない行列のときは、DirectXMathの決まりで無限大やNaNが入る)
	 */
	inline Matrix Inverse(const Matrix& matrix) { return ToMatrix(DirectX::XMMatrixInverse(nullptr, ToXMMATRIX(matrix))); }

	/**
	 * @brief 行と列を入れ替えた行列を返す
	 * @param matrix 元の行列
	 * @return 転置した行列
	 */
	inline Matrix Transpose(const Matrix& matrix) { return ToMatrix(DirectX::XMMatrixTranspose(ToXMMATRIX(matrix))); }

	/**
	 * @brief 点を動かす
	 * @details 平行移動も当てる。
	 * @param matrix 当てる行列
	 * @param point 動かす点
	 * @return 動かした点
	 */
	inline Vector3 TransformPoint(const Matrix& matrix, const Vector3& point)
	{
		return ToVector3(DirectX::XMVector3TransformCoord(ToXMVECTOR(point), ToXMMATRIX(matrix)));
	}

	/**
	 * @brief 向きを動かす
	 * @details 平行移動は当てず、回転と拡大縮小だけを当てる。
	 * @param matrix 当てる行列
	 * @param direction 動かす向き
	 * @return 動かした向き
	 */
	inline Vector3 TransformDirection(const Matrix& matrix, const Vector3& direction)
	{
		return ToVector3(DirectX::XMVector3TransformNormal(ToXMVECTOR(direction), ToXMMATRIX(matrix)));
	}


	// 作るための関数
	inline Matrix Matrix::Translation(const Vector3& translation)
	{
		return ToMatrix(DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z));
	}

	inline Matrix Matrix::Scaling(const Vector3& scale)
	{
		return ToMatrix(DirectX::XMMatrixScaling(scale.x, scale.y, scale.z));
	}

	inline Matrix Matrix::RotationX(float radians) { return ToMatrix(DirectX::XMMatrixRotationX(radians)); }

	inline Matrix Matrix::RotationY(float radians) { return ToMatrix(DirectX::XMMatrixRotationY(radians)); }

	inline Matrix Matrix::RotationZ(float radians) { return ToMatrix(DirectX::XMMatrixRotationZ(radians)); }

	inline Matrix Matrix::Rotation(const Quaternion& rotation)
	{
		return ToMatrix(DirectX::XMMatrixRotationQuaternion(ToXMVECTOR(rotation)));
	}

	inline Matrix Matrix::LookAt(const Vector3& eye, const Vector3& target, const Vector3& up)
	{
		return ToMatrix(DirectX::XMMatrixLookAtLH(ToXMVECTOR(eye), ToXMVECTOR(target), ToXMVECTOR(up)));
	}

	inline Matrix Matrix::Perspective(float verticalFieldOfView, float aspectRatio, float nearZ, float farZ)
	{
		return ToMatrix(DirectX::XMMatrixPerspectiveFovLH(verticalFieldOfView, aspectRatio, nearZ, farZ));
	}
} // namespace fangEngine::core
