/**
 * @file Math.h
 * @brief 数1つを扱う計算の関数をまとめたファイル
 * @details 角度はすべてラジアンで扱う。
 */
#pragma once
#include <cmath>


namespace fangEngine::core
{
	constexpr float PI                     = 3.14159265358979323846f; // 円周率
	constexpr float NEARLY_EQUAL_TOLERANCE = 1.0e-5f;                 // floatを等しいとみなす差の既定値

	/**
	 * @brief 度をラジアンに変える
	 * @param degrees 度で表した角度
	 * @return ラジアンで表した角度
	 */
	constexpr float DegreesToRadians(float degrees) { return degrees * (PI / 180.0f); }

	/**
	 * @brief ラジアンを度に変える
	 * @param radians ラジアンで表した角度
	 * @return 度で表した角度
	 */
	constexpr float RadiansToDegrees(float radians) { return radians * (180.0f / PI); }

	/**
	 * @brief 値を範囲の中に収める
	 * @param value 収める値
	 * @param minimum 範囲の下限
	 * @param maximum 範囲の上限
	 * @return 範囲に収めた値
	 */
	constexpr float Clamp(float value, float minimum, float maximum)
	{
		if (value < minimum) return minimum;
		if (value > maximum) return maximum;
		return value;
	}

	/**
	 * @brief 値を0から1の範囲に収める
	 * @param value 収める値
	 * @return 0から1に収めた値
	 */
	constexpr float Saturate(float value) { return Clamp(value, 0.0f, 1.0f); }

	/**
	 * @brief 2つの値の間を割合で取る
	 * @param start 割合が0のときの値
	 * @param end 割合が1のときの値
	 * @param rate 割合(0から1の外は収めない)
	 * @return startとendの間の値
	 */
	constexpr float Lerp(float start, float end, float rate) { return start + (end - start) * rate; }

	/**
	 * @brief 値が2つの値の間のどの割合にあるかを返す
	 * @details Lerpの逆の計算。startとendが同じときは0を返す。
	 * @param start 割合が0になる値
	 * @param end 割合が1になる値
	 * @param value 割合を知りたい値
	 * @return valueの割合(0から1の外は収めない)
	 */
	constexpr float InverseLerp(float start, float end, float value)
	{
		if (start == end) return 0.0f;
		return (value - start) / (end - start);
	}

	/**
	 * @brief ある範囲の値を、別の範囲の値に移し替える
	 * @details スティックの遊びを除いて0から1に直すときに使う。結果は出力の範囲に収める。
	 * @param value 移し替える値
	 * @param inputStart 入力の範囲の始まり
	 * @param inputEnd 入力の範囲の終わり
	 * @param outputStart 出力の範囲の始まり
	 * @param outputEnd 出力の範囲の終わり
	 * @return 出力の範囲に移し替えた値
	 */
	constexpr float Remap(float value, float inputStart, float inputEnd, float outputStart, float outputEnd)
	{
		return Lerp(outputStart, outputEnd, Saturate(InverseLerp(inputStart, inputEnd, value)));
	}

	/**
	 * @brief 2つの値の間を、始まりと終わりが緩やかになる曲線で0から1に変える
	 * @details UIのフェードの出入りを柔らかくするときに使う。
	 * @param start 0になる値
	 * @param end 1になる値
	 * @param value 変える値
	 * @return 0から1の値
	 */
	constexpr float SmoothStep(float start, float end, float value)
	{
		const float rate = Saturate(InverseLerp(start, end, value));
		return rate * rate * (3.0f - 2.0f * rate);
	}

	/**
	 * @brief 目標へ決まった量だけ近づける
	 * @param current 今の値
	 * @param target 目標の値
	 * @param maxDelta 1回で動かす量の上限
	 * @return 近づけた値(目標を通り越さない)
	 */
	constexpr float MoveTowards(float current, float target, float maxDelta)
	{
		const float delta = target - current;
		if (delta <= maxDelta && delta >= -maxDelta) return target;
		return current + (delta > 0.0f ? maxDelta : -maxDelta);
	}

	/**
	 * @brief フレームレートに左右されずに、目標へ滑らかに近づける
	 * @details 毎フレームLerpで追うと、追いつく速さがフレームレートで変わる。
	 * 割合をdeltaTimeから決めるため、30fpsでも60fpsでも同じ速さで追う。
	 * @param current 今の値
	 * @param target 目標の値
	 * @param speed 近づく速さ(大きいほど早く追いつく)
	 * @param deltaTime 前のフレームからの秒数
	 * @return 近づけた値
	 */
	inline float Damp(float current, float target, float speed, float deltaTime)
	{
		return Lerp(current, target, 1.0f - std::exp(-speed * deltaTime));
	}

	/**
	 * @brief 速さを持ったまま、目標へ滑らかに近づける
	 * @details 速さを引き継ぐため、Dampより動き出しと止まり際が滑らかになる。カメラの追従に使う。
	 * @param current 今の値
	 * @param target 目標の値
	 * @param velocity 今の速さ(呼ぶ側が持ち続け、この関数が書き換える)
	 * @param smoothTime 目標に届くまでのおおよその秒数
	 * @param deltaTime 前のフレームからの秒数
	 * @return 近づけた値
	 */
	inline float SmoothDamp(float current, float target, float& velocity, float smoothTime, float deltaTime)
	{
		// 0で割らないため
		if (smoothTime < 1.0e-4f) smoothTime = 1.0e-4f;

		// ばねの減衰をexpの近似で解く
		const float omega   = 2.0f / smoothTime;
		const float x       = omega * deltaTime;
		const float decay   = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		const float change  = current - target;
		const float impulse = (velocity + omega * change) * deltaTime;
		velocity            = (velocity - omega * impulse) * decay;
		float result        = target + (change + impulse) * decay;

		// 目標を通り越したら、目標で止める
		if ((target - current > 0.0f) == (result > target)) {
			result   = target;
			velocity = 0.0f;
		}
		return result;
	}

	/**
	 * @brief 角度を-πからπの範囲に収める
	 * @param radians 収める角度
	 * @return -πからπの角度
	 */
	inline float WrapAngle(float radians) { return std::remainder(radians, 2.0f * PI); }

	/**
	 * @brief 今の角度から目標の角度まで、近いほうの回り方で何ラジアンあるかを返す
	 * @param current 今の角度
	 * @param target 目標の角度
	 * @return -πからπの差(正なら角度が増える向きに回る)
	 */
	inline float DeltaAngle(float current, float target) { return WrapAngle(target - current); }

	/**
	 * @brief 近いほうの回り方で、目標の角度へ決まった量だけ近づける
	 * @param current 今の角度
	 * @param target 目標の角度
	 * @param maxDelta 1回で回す角度の上限
	 * @return 近づけた角度(-πからπ)
	 */
	inline float MoveTowardsAngle(float current, float target, float maxDelta)
	{
		return WrapAngle(current + Clamp(DeltaAngle(current, target), -maxDelta, maxDelta));
	}

	/**
	 * @brief 誤差を許して2つのfloatを比べる
	 * @param a 比べる値
	 * @param b 比べる値
	 * @param tolerance 等しいとみなす差
	 * @return 差がtolerance以下ならtrue、超えたらfalse
	 */
	inline bool NearlyEqual(float a, float b, float tolerance = NEARLY_EQUAL_TOLERANCE)
	{
		return std::fabs(a - b) <= tolerance;
	}
} // namespace fangEngine::core
