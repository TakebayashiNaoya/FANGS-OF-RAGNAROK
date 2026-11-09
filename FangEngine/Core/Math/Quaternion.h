/**
 * @file Quaternion.h
 * @brief 回転を表すクォータニオンの型と、その計算をまとめたファイル
 * @details 計算はDirectXMathで行う。glTFと同じx、y、z、wの並びで持つ。
 */
#pragma once
#include <DirectXMath.h>
#include <cmath>
#include "Core/Math/Math.h"
#include "Core/Math/Vector.h"


namespace fangEngine::core
{
	/**
	 * @brief 回転を表すクォータニオン
	 * @details 掛け算の順番はMatrixと同じで、a * bはaで回してからbで回す。
	 */
	struct Quaternion
	{
		float x = 0.0f; // 回転軸のX成分にsin(角度/2)を掛けた値
		float y = 0.0f; // 回転軸のY成分にsin(角度/2)を掛けた値
		float z = 0.0f; // 回転軸のZ成分にsin(角度/2)を掛けた値
		float w = 1.0f; // cos(角度/2)

		static const Quaternion IDENTITY; // 回さない回転

		/**
		 * @brief 軸の周りに回す回転を作る
		 * @param axis 回す軸(長さは1でなくてよい)
		 * @param radians 回す角度
		 * @return 回転
		 */
		static Quaternion AngleAxis(const Vector3& axis, float radians);

		/**
		 * @brief X、Y、Z軸の周りの角度から回転を作る
		 * @details Z、X、Yの順に回す。
		 * @param pitch X軸の周りの角度(上下)
		 * @param yaw Y軸の周りの角度(左右)
		 * @param roll Z軸の周りの角度(傾き)
		 * @return 回転
		 */
		static Quaternion FromEuler(float pitch, float yaw, float roll);

		/**
		 * @brief +Zをforwardへ向ける回転を作る
		 * @details 進む向きへ体を向けるときに使う。
		 * @param forward 向ける前の向き
		 * @param up 上の向き(forwardと平行なときは、別の向きを代わりに使う)
		 * @return 回転(forwardの長さが0に近いときはIDENTITY)
		 */
		static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3::UP);
	};

	inline constexpr Quaternion Quaternion::IDENTITY{ 0.0f, 0.0f, 0.0f, 1.0f };


	/**
	 * @brief DirectXMathで計算するために、レジスタへ載せる
	 * @param quaternion 載せる回転
	 * @return レジスタの値
	 */
	inline DirectX::XMVECTOR ToXMVECTOR(const Quaternion& quaternion)
	{
		return DirectX::XMVectorSet(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
	}

	/**
	 * @brief DirectXMathで計算した値を、レジスタから戻す
	 * @param quaternion レジスタの値
	 * @return 戻した回転
	 */
	inline Quaternion ToQuaternion(DirectX::FXMVECTOR quaternion)
	{
		DirectX::XMFLOAT4 result{};
		DirectX::XMStoreFloat4(&result, quaternion);
		return Quaternion{ result.x, result.y, result.z, result.w };
	}

	/**
	 * @brief 2つの回転をつなぐ
	 * @param a 先に回す回転
	 * @param b 後から回す回転
	 * @return aで回してからbで回す回転
	 */
	inline Quaternion operator*(const Quaternion& a, const Quaternion& b)
	{
		return ToQuaternion(DirectX::XMQuaternionMultiply(ToXMVECTOR(a), ToXMVECTOR(b)));
	}

	/**
	 * @brief 内積を返す
	 * @param a 掛ける回転
	 * @param b 掛ける回転
	 * @return 内積
	 */
	constexpr float Dot(const Quaternion& a, const Quaternion& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

	/**
	 * @brief 長さを1にした回転を返す
	 * @details 掛け算を重ねてずれた長さを戻すために使う。
	 * @param quaternion 元の回転
	 * @return 長さが1の回転
	 */
	inline Quaternion Normalize(const Quaternion& quaternion)
	{
		return ToQuaternion(DirectX::XMQuaternionNormalize(ToXMVECTOR(quaternion)));
	}

	/**
	 * @brief 逆の回転を返す
	 * @param quaternion 元の回転
	 * @return 逆の回転
	 */
	inline Quaternion Inverse(const Quaternion& quaternion)
	{
		return ToQuaternion(DirectX::XMQuaternionInverse(ToXMVECTOR(quaternion)));
	}

	/**
	 * @brief 2つの回転の間の角度を返す
	 * @param a 1つ目の回転
	 * @param b 2つ目の回転
	 * @return 0からπの角度
	 */
	inline float Angle(const Quaternion& a, const Quaternion& b)
	{
		return 2.0f * std::acos(Clamp(std::fabs(Dot(a, b)), 0.0f, 1.0f));
	}

	/**
	 * @brief 2つの回転の間を、回る速さが一定になるように割合で取る
	 * @details 近いほうの回り方を選ぶ。
	 * @param start 割合が0のときの回転
	 * @param end 割合が1のときの回転
	 * @param rate 割合
	 * @return startとendの間の回転
	 */
	inline Quaternion Slerp(const Quaternion& start, const Quaternion& end, float rate)
	{
		return ToQuaternion(DirectX::XMQuaternionSlerp(ToXMVECTOR(start), ToXMVECTOR(end), rate));
	}

	/**
	 * @brief 目標の回転へ決まった角度だけ近づける
	 * @details 決まった速さで振り向かせるときに使う。
	 * @param from 今の回転
	 * @param to 目標の回転
	 * @param maxRadians 1回で回す角度の上限
	 * @return 近づけた回転(目標を通り越さない)
	 */
	inline Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, float maxRadians)
	{
		const float angle = Angle(from, to);
		if (angle <= maxRadians) return to;
		return Slerp(from, to, maxRadians / angle);
	}

	/**
	 * @brief ベクトルを回す
	 * @param quaternion 回す回転
	 * @param vector 回すベクトル
	 * @return 回したベクトル
	 */
	inline Vector3 Rotate(const Quaternion& quaternion, const Vector3& vector)
	{
		return ToVector3(DirectX::XMVector3Rotate(ToXMVECTOR(vector), ToXMVECTOR(quaternion)));
	}


	// 作るための関数
	inline Quaternion Quaternion::AngleAxis(const Vector3& axis, float radians)
	{
		return ToQuaternion(DirectX::XMQuaternionRotationAxis(ToXMVECTOR(axis), radians));
	}

	inline Quaternion Quaternion::FromEuler(float pitch, float yaw, float roll)
	{
		return ToQuaternion(DirectX::XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
	}

	inline Quaternion Quaternion::LookRotation(const Vector3& forward, const Vector3& up)
	{
		const Vector3 newForward = Normalize(forward);
		if (LengthSquared(newForward) == 0.0f) return IDENTITY;

		// forwardとupが平行だと右の向きが決まらないため、upの代わりに+Zを使う
		Vector3 newRight = Normalize(Cross(up, newForward));
		if (LengthSquared(newRight) == 0.0f) {
			newRight = Normalize(Cross(Vector3::FORWARD, newForward));
		}
		const Vector3 newUp = Cross(newForward, newRight);

		// 行ベクトルの決まりのため、各行に回したあとの軸を並べる
		const DirectX::XMMATRIX rotation(
			newRight.x, newRight.y, newRight.z, 0.0f,
			newUp.x, newUp.y, newUp.z, 0.0f,
			newForward.x, newForward.y, newForward.z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);
		return ToQuaternion(DirectX::XMQuaternionRotationMatrix(rotation));
	}
} // namespace fangEngine::core
