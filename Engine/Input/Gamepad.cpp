/**
 * @file Gamepad.cpp
 * @brief パッドの状態のうち、プラットフォームに依らない部分。
 * @details 読み取りそのものは Windows/ と Xbox/ にある。
 */
#include "Pch.h"
#include "Input/Gamepad.h"
#include <cmath>


namespace fang
{
	bool IsButtonDown(const GamepadState& state, EnGamepadButton button)
	{
		return (state.buttons & static_cast<uint32_t>(button)) != 0;
	}


	Vector2 ApplyRadialDeadZone(float axisX, float axisY)
	{
		const float lengthSquared = axisX * axisX + axisY * axisY;
		if (lengthSquared <= STICK_DEAD_ZONE * STICK_DEAD_ZONE)
		{
			return Vector2{};
		}

		const float length = std::sqrt(lengthSquared);

		// デッドゾーンの外側を 0 〜 1 に引き伸ばす。境目で 0 から始まるので、動き出しが跳ねない。
		float scaledLength = (length - STICK_DEAD_ZONE) / (STICK_SATURATION_ZONE - STICK_DEAD_ZONE);
		if (scaledLength > 1.0f)
		{
			scaledLength = 1.0f;
		}

		// 向きは変えずに長さだけ差し替える。長さは上でデッドゾーンを超えていると確かめてある。
		const float inverseLength = scaledLength / length;

		return Vector2{ axisX * inverseLength, axisY * inverseLength };
	}


	Vector2 GetLeftStick(const GamepadState& state)
	{
		return ApplyRadialDeadZone(state.leftStickX, state.leftStickY);
	}


	Vector2 GetRightStick(const GamepadState& state)
	{
		return ApplyRadialDeadZone(state.rightStickX, state.rightStickY);
	}
} // namespace fang
