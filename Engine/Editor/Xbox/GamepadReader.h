/**
 * @file GamepadReader.h
 * @brief パッドの状態を WinRT の型を出さずに受け渡すための境界（Xbox / UWP）。
 */
#pragma once

#include <cstdint>


namespace fang::editor
{
	/** @brief パッドのボタン。GamepadState::buttons のビット位置。 */
	enum class EnGamepadButton : uint32_t
	{
		Menu            = 1u << 0,
		View            = 1u << 1,
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

	/** @brief パッド 1 台分の 1 フレームの状態。WinRT の型を外へ出さないための入れ物。 */
	struct GamepadState
	{
		bool     isConnected = false;
		uint32_t buttons     = 0; /**< EnGamepadButton のビット和。 */

		float leftStickX  = 0.0f; /**< 左が -1、右が +1。デッドゾーンをかける前の生値。 */
		float leftStickY  = 0.0f; /**< 下が -1、上が +1。 */
		float rightStickX = 0.0f;
		float rightStickY = 0.0f;

		float leftTrigger  = 0.0f; /**< 0 〜 1。 */
		float rightTrigger = 0.0f;
	};

	/**
	 * @brief パッドの今の状態を 1 台分読む。
	 * @details 繋がっていなければ isConnected = false のまま返る。WinRT の例外はこの中で止める。
	 * @return 読めなかったときは全項目が初期値のまま返る。呼び出し側は isConnected だけ見ればよい。
	 * @threading メインスレッドのみ。
	 */
	[[nodiscard]] GamepadState ReadGamepadState();

	/** @brief ボタンが押されているか。 */
	[[nodiscard]] bool IsButtonDown(const GamepadState& state, EnGamepadButton button);
} // namespace fang::editor
