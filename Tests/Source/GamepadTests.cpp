/**
 * @file GamepadTests.cpp
 * @brief パッドのテスト。丸いデッドゾーンの切り方と、ボタンのビットを確かめる。
 */
#include "Core/Math/Vector2.h"
#include "Input/Input.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 倒し量の長さ。 */
	float GetLength(const fang::Vector2& value)
	{
		return std::sqrt(value.x * value.x + value.y * value.y);
	}
} // namespace


TEST_CASE("デッドゾーンの内側は 0 になる")
{
	// 手を離してもスティックはわずかに傾いている。ここで消えないと狼が勝手に動く。
	CHECK(GetLength(fang::ApplyRadialDeadZone(0.0f, 0.0f)) == doctest::Approx(0.0f));
	CHECK(GetLength(fang::ApplyRadialDeadZone(0.1f, 0.0f)) == doctest::Approx(0.0f));
	CHECK(GetLength(fang::ApplyRadialDeadZone(0.15f, 0.15f)) == doctest::Approx(0.0f));

	// 境目のすぐ内側。
	CHECK(GetLength(fang::ApplyRadialDeadZone(fang::STICK_DEAD_ZONE - 0.01f, 0.0f)) == doctest::Approx(0.0f));
}


TEST_CASE("デッドゾーンの外側は 0 から 1 へ均される")
{
	// 境目のすぐ外側は 0 に近い ➡ 動き出しが跳ねない。
	const fang::Vector2 justOutside = fang::ApplyRadialDeadZone(fang::STICK_DEAD_ZONE + 0.01f, 0.0f);
	CHECK(GetLength(justOutside) > 0.0f);
	CHECK(GetLength(justOutside) < 0.05f);

	// 振り切り点まで倒せば 1。それ以上倒しても 1 のまま。
	CHECK(GetLength(fang::ApplyRadialDeadZone(fang::STICK_SATURATION_ZONE, 0.0f)) == doctest::Approx(1.0f));
	CHECK(GetLength(fang::ApplyRadialDeadZone(1.0f, 0.0f)) == doctest::Approx(1.0f));
}


TEST_CASE("斜めに倒しても向きが変わらず、長さが 1 を超えない")
{
	// 軸ごとに切ると、斜めの入力が最大 √2 倍に見えたり軸へ吸い付いたりする。丸く切ればどちらも起きない。
	const float         diagonal = 1.0f / std::sqrt(2.0f);
	const fang::Vector2 result   = fang::ApplyRadialDeadZone(diagonal, diagonal);

	CHECK(GetLength(result) <= 1.0f + 0.001f);

	// 45 度の向きが保たれている。
	CHECK(result.x == doctest::Approx(result.y));

	// 斜めに振り切った場合も長さは 1 で頭打ち。
	CHECK(GetLength(fang::ApplyRadialDeadZone(1.0f, 1.0f)) == doctest::Approx(1.0f));
}


TEST_CASE("倒した向きが 4 方向で正しく出る")
{
	const fang::Vector2 up = fang::ApplyRadialDeadZone(0.0f, 1.0f);
	CHECK(up.x == doctest::Approx(0.0f));
	CHECK(up.y == doctest::Approx(1.0f));

	const fang::Vector2 down = fang::ApplyRadialDeadZone(0.0f, -1.0f);
	CHECK(down.y == doctest::Approx(-1.0f));

	const fang::Vector2 left = fang::ApplyRadialDeadZone(-1.0f, 0.0f);
	CHECK(left.x == doctest::Approx(-1.0f));

	const fang::Vector2 right = fang::ApplyRadialDeadZone(1.0f, 0.0f);
	CHECK(right.x == doctest::Approx(1.0f));
}


TEST_CASE("左右のスティックがそれぞれの軸を読む")
{
	fang::GamepadState state;
	state.isConnected = true;
	state.leftStickX  = 1.0f;
	state.rightStickY = -1.0f;

	CHECK(fang::GetLeftStick(state).x == doctest::Approx(1.0f));
	CHECK(fang::GetLeftStick(state).y == doctest::Approx(0.0f));

	CHECK(fang::GetRightStick(state).x == doctest::Approx(0.0f));
	CHECK(fang::GetRightStick(state).y == doctest::Approx(-1.0f));
}


TEST_CASE("ボタンのビットが混ざらない")
{
	fang::GamepadState state;
	state.buttons =
		static_cast<uint32_t>(fang::EnGamepadButton::A) | static_cast<uint32_t>(fang::EnGamepadButton::DPadLeft);

	CHECK(fang::IsButtonDown(state, fang::EnGamepadButton::A));
	CHECK(fang::IsButtonDown(state, fang::EnGamepadButton::DPadLeft));

	CHECK_FALSE(fang::IsButtonDown(state, fang::EnGamepadButton::B));
	CHECK_FALSE(fang::IsButtonDown(state, fang::EnGamepadButton::Menu));
	CHECK_FALSE(fang::IsButtonDown(state, fang::EnGamepadButton::RightThumbstick));
}


TEST_CASE("繋がっていないパッドは倒し量 0 を返す")
{
	const fang::GamepadState state;

	CHECK_FALSE(state.isConnected);
	CHECK(GetLength(fang::GetLeftStick(state)) == doctest::Approx(0.0f));
	CHECK(GetLength(fang::GetRightStick(state)) == doctest::Approx(0.0f));
}
