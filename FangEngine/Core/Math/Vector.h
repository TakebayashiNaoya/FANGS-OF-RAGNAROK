/**
 * @file Vector.h
 * @brief 2次元から4次元のベクトルの型と、ベクトルどうしの計算をまとめたファイル
 * @details 座標は左手系で、右が+X、上が+Y、前が+Z。
 * 3つか4つの数の計算はレジスタへの出し入れで速さが変わらないため、DirectXMathを通さずに書く。
 */
#pragma once
#include <DirectXMath.h>
#include <cmath>
#include "Core/Math/Math.h"


namespace fangEngine::core
{
	/**
	 * @brief 2次元のベクトル
	 */
	struct Vector2
	{
		float x = 0.0f; // X成分
		float y = 0.0f; // Y成分

		static const Vector2 ZERO; // すべて0
		static const Vector2 ONE;  // すべて1
	};

	/**
	 * @brief 3次元のベクトル
	 */
	struct Vector3
	{
		float x = 0.0f; // X成分
		float y = 0.0f; // Y成分
		float z = 0.0f; // Z成分

		static const Vector3 ZERO;    // すべて0
		static const Vector3 ONE;     // すべて1
		static const Vector3 RIGHT;   // 右(+X)
		static const Vector3 UP;      // 上(+Y)
		static const Vector3 FORWARD; // 前(+Z)
	};

	/**
	 * @brief 4次元のベクトル
	 * @details 色(RGBA)や、wを持つ座標に使う。
	 */
	struct Vector4
	{
		float x = 0.0f; // X成分
		float y = 0.0f; // Y成分
		float z = 0.0f; // Z成分
		float w = 0.0f; // W成分

		static const Vector4 ZERO; // すべて0
		static const Vector4 ONE;  // すべて1
	};

	inline constexpr Vector2 Vector2::ZERO{ 0.0f, 0.0f };
	inline constexpr Vector2 Vector2::ONE{ 1.0f, 1.0f };
	inline constexpr Vector3 Vector3::ZERO{ 0.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::ONE{ 1.0f, 1.0f, 1.0f };
	inline constexpr Vector3 Vector3::RIGHT{ 1.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::UP{ 0.0f, 1.0f, 0.0f };
	inline constexpr Vector3 Vector3::FORWARD{ 0.0f, 0.0f, 1.0f };
	inline constexpr Vector4 Vector4::ZERO{ 0.0f, 0.0f, 0.0f, 0.0f };
	inline constexpr Vector4 Vector4::ONE{ 1.0f, 1.0f, 1.0f, 1.0f };


	// Vector2の計算
	constexpr Vector2 operator-(const Vector2& vector) { return Vector2{ -vector.x, -vector.y }; }
	constexpr Vector2 operator+(const Vector2& a, const Vector2& b) { return Vector2{ a.x + b.x, a.y + b.y }; }
	constexpr Vector2 operator-(const Vector2& a, const Vector2& b) { return Vector2{ a.x - b.x, a.y - b.y }; }
	constexpr Vector2 operator*(const Vector2& vector, float scale) { return Vector2{ vector.x * scale, vector.y * scale }; }
	constexpr Vector2 operator*(float scale, const Vector2& vector) { return vector * scale; }
	constexpr Vector2 operator/(const Vector2& vector, float scale) { return Vector2{ vector.x / scale, vector.y / scale }; }
	constexpr Vector2& operator+=(Vector2& a, const Vector2& b) { return a = a + b; }
	constexpr Vector2& operator-=(Vector2& a, const Vector2& b) { return a = a - b; }
	constexpr Vector2& operator*=(Vector2& vector, float scale) { return vector = vector * scale; }
	constexpr Vector2& operator/=(Vector2& vector, float scale) { return vector = vector / scale; }

	/**
	 * @brief 内積を返す
	 * @param a 掛けるベクトル
	 * @param b 掛けるベクトル
	 * @return 内積
	 */
	constexpr float Dot(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }

	/**
	 * @brief 長さの2乗を返す
	 * @details 長さを比べるだけなら平方根が要らないため、Lengthより軽い。
	 * @param vector 長さを知りたいベクトル
	 * @return 長さの2乗
	 */
	constexpr float LengthSquared(const Vector2& vector) { return Dot(vector, vector); }

	/**
	 * @brief 長さを返す
	 * @param vector 長さを知りたいベクトル
	 * @return 長さ
	 */
	inline float Length(const Vector2& vector) { return std::sqrt(LengthSquared(vector)); }

	/**
	 * @brief 向きが同じで長さが1のベクトルを返す
	 * @param vector 元のベクトル
	 * @return 長さが1のベクトル(長さが0に近いときはZERO)
	 */
	inline Vector2 Normalize(const Vector2& vector)
	{
		const float length = Length(vector);
		if (length < NEARLY_EQUAL_TOLERANCE) return Vector2::ZERO;
		return vector / length;
	}

	/**
	 * @brief 2点の間の距離を返す
	 * @param a 1つ目の点
	 * @param b 2つ目の点
	 * @return 距離
	 */
	inline float Distance(const Vector2& a, const Vector2& b) { return Length(b - a); }

	/**
	 * @brief 2つのベクトルの間を割合で取る
	 * @param start 割合が0のときのベクトル
	 * @param end 割合が1のときのベクトル
	 * @param rate 割合(0から1の外は収めない)
	 * @return startとendの間のベクトル
	 */
	constexpr Vector2 Lerp(const Vector2& start, const Vector2& end, float rate) { return start + (end - start) * rate; }


	// Vector3の計算
	constexpr Vector3 operator-(const Vector3& vector) { return Vector3{ -vector.x, -vector.y, -vector.z }; }
	constexpr Vector3 operator+(const Vector3& a, const Vector3& b) { return Vector3{ a.x + b.x, a.y + b.y, a.z + b.z }; }
	constexpr Vector3 operator-(const Vector3& a, const Vector3& b) { return Vector3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
	constexpr Vector3 operator*(const Vector3& vector, float scale) { return Vector3{ vector.x * scale, vector.y * scale, vector.z * scale }; }
	constexpr Vector3 operator*(float scale, const Vector3& vector) { return vector * scale; }
	constexpr Vector3 operator/(const Vector3& vector, float scale) { return Vector3{ vector.x / scale, vector.y / scale, vector.z / scale }; }
	constexpr Vector3& operator+=(Vector3& a, const Vector3& b) { return a = a + b; }
	constexpr Vector3& operator-=(Vector3& a, const Vector3& b) { return a = a - b; }
	constexpr Vector3& operator*=(Vector3& vector, float scale) { return vector = vector * scale; }
	constexpr Vector3& operator/=(Vector3& vector, float scale) { return vector = vector / scale; }

	/**
	 * @brief 内積を返す
	 * @param a 掛けるベクトル
	 * @param b 掛けるベクトル
	 * @return 内積
	 */
	constexpr float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

	/**
	 * @brief 外積を返す
	 * @details 左手系のため、Cross(RIGHT, UP)はFORWARDになる。
	 * @param a 掛けるベクトル
	 * @param b 掛けるベクトル
	 * @return aとbの両方に垂直なベクトル
	 */
	constexpr Vector3 Cross(const Vector3& a, const Vector3& b)
	{
		return Vector3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	/**
	 * @brief 長さの2乗を返す
	 * @details 長さを比べるだけなら平方根が要らないため、Lengthより軽い。
	 * @param vector 長さを知りたいベクトル
	 * @return 長さの2乗
	 */
	constexpr float LengthSquared(const Vector3& vector) { return Dot(vector, vector); }

	/**
	 * @brief 長さを返す
	 * @param vector 長さを知りたいベクトル
	 * @return 長さ
	 */
	inline float Length(const Vector3& vector) { return std::sqrt(LengthSquared(vector)); }

	/**
	 * @brief 向きが同じで長さが1のベクトルを返す
	 * @param vector 元のベクトル
	 * @return 長さが1のベクトル(長さが0に近いときはZERO)
	 */
	inline Vector3 Normalize(const Vector3& vector)
	{
		const float length = Length(vector);
		if (length < NEARLY_EQUAL_TOLERANCE) return Vector3::ZERO;
		return vector / length;
	}

	/**
	 * @brief 2点の間の距離を返す
	 * @param a 1つ目の点
	 * @param b 2つ目の点
	 * @return 距離
	 */
	inline float Distance(const Vector3& a, const Vector3& b) { return Length(b - a); }

	/**
	 * @brief 2つのベクトルの間を割合で取る
	 * @param start 割合が0のときのベクトル
	 * @param end 割合が1のときのベクトル
	 * @param rate 割合(0から1の外は収めない)
	 * @return startとendの間のベクトル
	 */
	constexpr Vector3 Lerp(const Vector3& start, const Vector3& end, float rate) { return start + (end - start) * rate; }

	/**
	 * @brief 長さが上限を超えないように縮める
	 * @details 斜めに倒したスティックの長さを1に収めるときに使う。
	 * @param vector 縮めるベクトル
	 * @param maxLength 長さの上限
	 * @return 長さをmaxLength以下にしたベクトル
	 */
	inline Vector3 ClampLength(const Vector3& vector, float maxLength)
	{
		const float lengthSquared = LengthSquared(vector);
		if (lengthSquared <= maxLength * maxLength) return vector;
		return vector * (maxLength / std::sqrt(lengthSquared));
	}

	/**
	 * @brief ベクトルを平面に落とす
	 * @details カメラの前の向きを地面に落として、カメラから見た向きで歩かせるときに使う。
	 * @param vector 落とすベクトル
	 * @param planeNormal 平面の法線(長さは1でなくてよい)
	 * @return 平面に沿ったベクトル(法線の長さが0に近いときはvectorのまま)
	 */
	constexpr Vector3 ProjectOnPlane(const Vector3& vector, const Vector3& planeNormal)
	{
		const float normalLengthSquared = LengthSquared(planeNormal);
		if (normalLengthSquared < NEARLY_EQUAL_TOLERANCE * NEARLY_EQUAL_TOLERANCE) return vector;
		return vector - planeNormal * (Dot(vector, planeNormal) / normalLengthSquared);
	}

	/**
	 * @brief 2つのベクトルの間の角度を返す
	 * @param from 1つ目のベクトル
	 * @param to 2つ目のベクトル
	 * @return 0からπの角度(どちらかの長さが0に近いときは0)
	 */
	inline float Angle(const Vector3& from, const Vector3& to)
	{
		const float lengthProduct = std::sqrt(LengthSquared(from) * LengthSquared(to));
		if (lengthProduct < NEARLY_EQUAL_TOLERANCE) return 0.0f;
		return std::acos(Clamp(Dot(from, to) / lengthProduct, -1.0f, 1.0f));
	}

	/**
	 * @brief 軸の周りで、fromからtoへ回る向き付きの角度を返す
	 * @details 敵から見てプレイヤーが左右どちらにいるかを知るときに使う。
	 * @param from 1つ目のベクトル
	 * @param to 2つ目のベクトル
	 * @param axis 回る軸(UPなら、上から見て時計回りが正)
	 * @return -πからπの角度
	 */
	inline float SignedAngle(const Vector3& from, const Vector3& to, const Vector3& axis)
	{
		const float angle = Angle(from, to);
		return Dot(axis, Cross(from, to)) < 0.0f ? -angle : angle;
	}

	/**
	 * @brief DirectXMathで計算するために、レジスタへ載せる
	 * @param vector 載せるベクトル
	 * @return wを0にしたレジスタの値
	 */
	inline DirectX::XMVECTOR ToXMVECTOR(const Vector3& vector) { return DirectX::XMVectorSet(vector.x, vector.y, vector.z, 0.0f); }

	/**
	 * @brief DirectXMathで計算した値を、レジスタから戻す
	 * @param vector レジスタの値
	 * @return x、y、zを戻したベクトル
	 */
	inline Vector3 ToVector3(DirectX::FXMVECTOR vector)
	{
		DirectX::XMFLOAT3 result{};
		DirectX::XMStoreFloat3(&result, vector);
		return Vector3{ result.x, result.y, result.z };
	}


	// Vector4の計算
	constexpr Vector4 operator-(const Vector4& vector) { return Vector4{ -vector.x, -vector.y, -vector.z, -vector.w }; }
	constexpr Vector4 operator+(const Vector4& a, const Vector4& b) { return Vector4{ a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
	constexpr Vector4 operator-(const Vector4& a, const Vector4& b) { return Vector4{ a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
	constexpr Vector4 operator*(const Vector4& vector, float scale) { return Vector4{ vector.x * scale, vector.y * scale, vector.z * scale, vector.w * scale }; }
	constexpr Vector4 operator*(float scale, const Vector4& vector) { return vector * scale; }
	constexpr Vector4 operator/(const Vector4& vector, float scale) { return Vector4{ vector.x / scale, vector.y / scale, vector.z / scale, vector.w / scale }; }
	constexpr Vector4& operator+=(Vector4& a, const Vector4& b) { return a = a + b; }
	constexpr Vector4& operator-=(Vector4& a, const Vector4& b) { return a = a - b; }
	constexpr Vector4& operator*=(Vector4& vector, float scale) { return vector = vector * scale; }
	constexpr Vector4& operator/=(Vector4& vector, float scale) { return vector = vector / scale; }

	/**
	 * @brief 内積を返す
	 * @param a 掛けるベクトル
	 * @param b 掛けるベクトル
	 * @return 内積
	 */
	constexpr float Dot(const Vector4& a, const Vector4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

	/**
	 * @brief 長さの2乗を返す
	 * @details 長さを比べるだけなら平方根が要らないため、Lengthより軽い。
	 * @param vector 長さを知りたいベクトル
	 * @return 長さの2乗
	 */
	constexpr float LengthSquared(const Vector4& vector) { return Dot(vector, vector); }

	/**
	 * @brief 長さを返す
	 * @param vector 長さを知りたいベクトル
	 * @return 長さ
	 */
	inline float Length(const Vector4& vector) { return std::sqrt(LengthSquared(vector)); }

	/**
	 * @brief 向きが同じで長さが1のベクトルを返す
	 * @param vector 元のベクトル
	 * @return 長さが1のベクトル(長さが0に近いときはZERO)
	 */
	inline Vector4 Normalize(const Vector4& vector)
	{
		const float length = Length(vector);
		if (length < NEARLY_EQUAL_TOLERANCE) return Vector4::ZERO;
		return vector / length;
	}

	/**
	 * @brief 2点の間の距離を返す
	 * @param a 1つ目の点
	 * @param b 2つ目の点
	 * @return 距離
	 */
	inline float Distance(const Vector4& a, const Vector4& b) { return Length(b - a); }

	/**
	 * @brief 2つのベクトルの間を割合で取る
	 * @param start 割合が0のときのベクトル
	 * @param end 割合が1のときのベクトル
	 * @param rate 割合(0から1の外は収めない)
	 * @return startとendの間のベクトル
	 */
	constexpr Vector4 Lerp(const Vector4& start, const Vector4& end, float rate) { return start + (end - start) * rate; }
} // namespace fangEngine::core
