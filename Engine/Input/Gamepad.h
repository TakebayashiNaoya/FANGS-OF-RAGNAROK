/**
 * @file Gamepad.h
 * @brief パッド 1 台の状態と、スティックの倒し量の均し方。
 * @details ここに OS の型を出さない。Windows.Gaming.Input も XInput も、読み取りの .cpp の中で閉じる。
 */
#pragma once

#include "Core/Math/Vector2.h"
#include <cstdint>


namespace fang
{
	/** @brief パッドのボタン。GamepadState::buttons のビット位置。 */
	enum class EnGamepadButton : uint32_t
	{
		Menu            = 1u << 0, /**< XInput では Start。 */
		View            = 1u << 1, /**< XInput では Back。 */
		A               = 1u << 2,
		B               = 1u << 3,
		X               = 1u << 4,
		Y               = 1u << 5,
		DPadUp          = 1u << 6,
		DPadDown        = 1u << 7,
		DPadLeft        = 1u << 8,
		DPadRight       = 1u << 9,
		LeftShoulder    = 1u << 10,
		RightShoulder   = 1u << 11,
		LeftThumbstick  = 1u << 12,
		RightThumbstick = 1u << 13,
	};

	/**
	 * @brief パッド 1 台分の 1 フレームの状態。
	 * @details 値を運ぶだけの POD。スティックはデッドゾーンをかける前の生値で、均すのは受け取った側の仕事。
	 */
	struct GamepadState
	{
		bool     isConnected = false;
		uint32_t buttons     = 0; /**< EnGamepadButton のビット和。 */

		float leftStickX  = 0.0f; /**< 左が -1、右が +1。 */
		float leftStickY  = 0.0f; /**< 下が -1、上が +1。 */
		float rightStickX = 0.0f;
		float rightStickY = 0.0f;

		float leftTrigger  = 0.0f; /**< 0 〜 1。 */
		float rightTrigger = 0.0f;
	};

	/**
	 * @brief ここまでは倒していないものとして扱う長さ。
	 * @details XInput の既定値（7849 / 32767）に合わせてある。Windows.Gaming.Input には対応する定数が
	 *          無いので、両方のプラットフォームでこの値を使う ➡ 操作感がプラットフォームで変わらない。
	 */
	inline constexpr float STICK_DEAD_ZONE = 0.24f;

	/** @brief ここまで倒したら振り切ったものとして扱う長さ。端まで倒し切らなくても最大速度が出る。 */
	inline constexpr float STICK_SATURATION_ZONE = 0.95f;

	/**
	 * @brief パッドの今の状態を 1 台分読む。
	 * @return 繋がっていなければ isConnected = false のまま返る。読めなかったときも同じ。
	 * @details 実装は Windows/ と Xbox/ にある。掴んだ 1 台を持ち続け、抜き差しのときだけ探し直すので、
	 *          定常状態のヒープ確保は 0。OS の例外はこの中で止める。
	 * @threading メインスレッドのみ。
	 */
	[[nodiscard]] GamepadState ReadGamepadState();

	/** @brief ボタンが押されているか。 */
	[[nodiscard]] bool IsButtonDown(const GamepadState& state, EnGamepadButton button);

	/**
	 * @brief スティックの倒し量に丸いデッドゾーンをかける。
	 * @param axisX 左が -1、右が +1。
	 * @param axisY 下が -1、上が +1。
	 * @return 向きはそのまま、長さを 0 〜 1 に均したもの。デッドゾーンの内側なら長さ 0。
	 * @details 軸ごとに切ると、斜めに倒したときの量が最大 √2 倍に見えたり、閾値の内側で軸に吸い付いたり
	 *          する ➡ 長さで切ってから均す。手を離したときのわずかな傾きもここで消える。
	 */
	[[nodiscard]] Vector2 ApplyRadialDeadZone(float axisX, float axisY);

	/** @brief 左スティックの倒し量。ApplyRadialDeadZone を通したもの。 */
	[[nodiscard]] Vector2 GetLeftStick(const GamepadState& state);

	/** @brief 右スティックの倒し量。ApplyRadialDeadZone を通したもの。 */
	[[nodiscard]] Vector2 GetRightStick(const GamepadState& state);
} // namespace fang
