/**
 * @file ImGuiPlatformInput.cpp
 * @brief ImGui への入力（Xbox / UWP）。パッドの状態を ImGui のナビゲーションに流す。
 */
#include "Pch.h"
#include "Editor/ImGuiPlatformInput.h"
#include "Core/CoreMacros.h"
#include "Input/Gamepad.h"
#include <imgui.h>


namespace fang::editor
{
	namespace
	{
		/** @brief ここまでは倒していないものとして扱う。手を離したときに選択枠が流れ続けるのを止める。 */
		constexpr float STICK_DEAD_ZONE = 0.30f;

		/** @brief ここまで倒したら振り切ったものとして扱う。実機の感触で詰める値。 */
		constexpr float STICK_SATURATION_ZONE = 0.90f;

		/**
		 * @brief 倒した量を 0 〜 1 に均す。ImGui 公式バックエンド（imgui_impl_win32）と同じ式。
		 * @param magnitude 0 以上の倒し量。軸の符号は呼び出し側で外しておく。
		 */
		float ApplyDeadZone(float magnitude)
		{
			const float normalized = (magnitude - STICK_DEAD_ZONE) / (STICK_SATURATION_ZONE - STICK_DEAD_ZONE);

			if (normalized < 0.0f)
			{
				return 0.0f;
			}

			if (normalized > 1.0f)
			{
				return 1.0f;
			}

			return normalized;
		}


		void AddButtonEvent(ImGuiIO& io, const GamepadState& state, ImGuiKey key, EnGamepadButton button)
		{
			io.AddKeyEvent(key, IsButtonDown(state, button));
		}


		/** @brief トリガーは 1 本が 1 キー。押し込み量をそのまま流す。 */
		void AddTriggerEvent(ImGuiIO& io, ImGuiKey key, float value)
		{
			const float amount = ApplyDeadZone(value);
			io.AddKeyAnalogEvent(key, amount > 0.0f, amount);
		}


		/**
		 * @brief 1 軸を 2 キーに割る。倒していない側には 0 を流して、押しっぱなし扱いを残さない。
		 * @param value 負の側が negativeKey、正の側が positiveKey。
		 */
		void AddAxisEvents(ImGuiIO& io, ImGuiKey negativeKey, ImGuiKey positiveKey, float value)
		{
			const float negativeAmount = ApplyDeadZone(value < 0.0f ? -value : 0.0f);
			const float positiveAmount = ApplyDeadZone(value > 0.0f ? value : 0.0f);

			io.AddKeyAnalogEvent(negativeKey, negativeAmount > 0.0f, negativeAmount);
			io.AddKeyAnalogEvent(positiveKey, positiveAmount > 0.0f, positiveAmount);
		}
	} // namespace


	void UpdateImGuiPlatformInput(void* windowHandle)
	{
		// Windows.Gaming.Input はウィンドウを見ないので、共有のシグネチャにある引数を使わない。
		FANG_UNUSED(windowHandle);

		ImGuiIO& io = ImGui::GetIO();

		const GamepadState state = ReadGamepadState();

		// ImGui のヘッダはこのフラグを「今 1 台繋がっている」の意味だと書いているので毎フレーム上げ下げする。
		// ➡起動した後に挿しても、抜いても、次のフレームで追いつく。
		if (state.isConnected)
		{
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
		}
		else
		{
			io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
		}

		// 未接続でも 24 種を全部流す。抜かれた瞬間に押していたものを離した扱いにするため、途中で返らない。
		AddButtonEvent(io, state, ImGuiKey_GamepadStart, EnGamepadButton::Menu);
		AddButtonEvent(io, state, ImGuiKey_GamepadBack, EnGamepadButton::View);
		AddButtonEvent(io, state, ImGuiKey_GamepadFaceLeft, EnGamepadButton::X);
		AddButtonEvent(io, state, ImGuiKey_GamepadFaceRight, EnGamepadButton::B);
		AddButtonEvent(io, state, ImGuiKey_GamepadFaceUp, EnGamepadButton::Y);
		AddButtonEvent(io, state, ImGuiKey_GamepadFaceDown, EnGamepadButton::A);
		AddButtonEvent(io, state, ImGuiKey_GamepadDpadUp, EnGamepadButton::DPadUp);
		AddButtonEvent(io, state, ImGuiKey_GamepadDpadDown, EnGamepadButton::DPadDown);
		AddButtonEvent(io, state, ImGuiKey_GamepadDpadLeft, EnGamepadButton::DPadLeft);
		AddButtonEvent(io, state, ImGuiKey_GamepadDpadRight, EnGamepadButton::DPadRight);
		AddButtonEvent(io, state, ImGuiKey_GamepadL1, EnGamepadButton::LeftShoulder);
		AddButtonEvent(io, state, ImGuiKey_GamepadR1, EnGamepadButton::RightShoulder);
		AddButtonEvent(io, state, ImGuiKey_GamepadL3, EnGamepadButton::LeftThumbstick);
		AddButtonEvent(io, state, ImGuiKey_GamepadR3, EnGamepadButton::RightThumbstick);

		AddTriggerEvent(io, ImGuiKey_GamepadL2, state.leftTrigger);
		AddTriggerEvent(io, ImGuiKey_GamepadR2, state.rightTrigger);

		// Y 軸は WinRT も ImGui も上が正なので、符号は反転しない。
		AddAxisEvents(io, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, state.leftStickX);
		AddAxisEvents(io, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, state.leftStickY);
		AddAxisEvents(io, ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight, state.rightStickX);
		AddAxisEvents(io, ImGuiKey_GamepadRStickDown, ImGuiKey_GamepadRStickUp, state.rightStickY);
	}
} // namespace fang::editor
