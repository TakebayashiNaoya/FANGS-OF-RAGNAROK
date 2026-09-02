/**
 * @file Matrix4x4.cpp
 * @brief 行列の積と、ビュー行列・透視投影行列の生成。
 * @details DirectXMath を使うのはこのファイルだけで、外へは型も名前も出さない。
 *          ➡上の層は素の POD だけを見ればよく、数学ライブラリを差し替えてもヘッダは変わらない。
 */
#include "Pch.h"
#include "Core/Math/Matrix4x4.h"
#include <DirectXMath.h>
#include <cstring>


namespace fang
{
	namespace
	{
		/**
		 * @brief DirectXMath の行列を Matrix4x4 へ写す。
		 * @details DirectXMath も行優先ストレージ + 行ベクトル規約なので、並べ替えも転置も要らない。
		 */
		Matrix4x4 ToMatrix4x4(const DirectX::XMMATRIX& source)
		{
			DirectX::XMFLOAT4X4 stored;
			DirectX::XMStoreFloat4x4(&stored, source);

			Matrix4x4 result;
			std::memcpy(result.m, stored.m, sizeof(result.m));
			return result;
		}


		/** @brief Matrix4x4 を DirectXMath の行列へ写す。 */
		DirectX::XMMATRIX ToXMMatrix(const Matrix4x4& source)
		{
			DirectX::XMFLOAT4X4 stored;
			std::memcpy(stored.m, source.m, sizeof(stored.m));
			return DirectX::XMLoadFloat4x4(&stored);
		}


		/**
		 * @brief Vector3 を DirectXMath のベクトルへ写す。
		 * @param w 位置なら 1、方向なら 0。
		 */
		DirectX::XMVECTOR ToXMVector(const Vector3& source, float w)
		{
			return DirectX::XMVectorSet(source.x, source.y, source.z, w);
		}
	} // namespace


	Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up)
	{
		const DirectX::XMVECTOR eyePosition    = ToXMVector(eye, 1.0f);
		const DirectX::XMVECTOR targetPosition = ToXMVector(target, 1.0f);
		const DirectX::XMVECTOR upDirection    = ToXMVector(up, 0.0f);

		FANG_ASSERT(!DirectX::XMVector3Equal(eyePosition, targetPosition), "視点と注視点が同じ位置にある");
		FANG_ASSERT(!DirectX::XMVector3Equal(upDirection, DirectX::XMVectorZero()), "上方向が 0 ベクトル");

		return ToMatrix4x4(DirectX::XMMatrixLookAtLH(eyePosition, targetPosition, upDirection));
	}


	Matrix4x4 MakePerspectiveMatrix(float fieldOfViewYRadians, float aspect, float nearZ, float farZ)
	{
		FANG_ASSERT(fieldOfViewYRadians > 0.0f, "垂直画角が 0 以下");
		FANG_ASSERT(fieldOfViewYRadians < DirectX::XM_PI, "垂直画角が広すぎる（pi 未満であること）");
		FANG_ASSERT(aspect > 0.0f, "縦横比が 0 以下");
		FANG_ASSERT(nearZ > 0.0f, "近平面までの距離が 0 以下");
		FANG_ASSERT(farZ > nearZ, "遠平面が近平面より手前にある");

		return ToMatrix4x4(DirectX::XMMatrixPerspectiveFovLH(fieldOfViewYRadians, aspect, nearZ, farZ));
	}


	Matrix4x4 Multiply(const Matrix4x4& left, const Matrix4x4& right)
	{
		return ToMatrix4x4(DirectX::XMMatrixMultiply(ToXMMatrix(left), ToXMMatrix(right)));
	}
} // namespace fang
