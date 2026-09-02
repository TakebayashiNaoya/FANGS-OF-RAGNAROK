/**
 * @file GamepadReader.cpp
 * @brief Windows.Gaming.Input からパッドを 1 台読む実装（Xbox / UWP）。
 */
#include "Pch.h"
#include "Editor/Xbox/GamepadReader.h"

// C++/WinRT（例外前提）を使ってよいのはこの Xbox ディレクトリの TU だけ。例外は外に出さない。
// この TU は imgui のヘッダを 1 本も include しない。プリプロセッサ定義を丸ごと上書きしている都合で
// IMGUI_DISABLE_OBSOLETE_FUNCTIONS が落ちるため、読むと ImGuiIO の大きさが他の TU とずれる。
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <chrono>


namespace fang::editor
{
	namespace
	{
		namespace winrt_input = winrt::Windows::Gaming::Input;

		/** @brief 未接続のときにパッドを探し直す間隔（秒）。毎フレーム探すと IVectorView が毎回 1 個作られる。 */
		constexpr float GAMEPAD_SEARCH_INTERVAL_IN_SECONDS = 1.0f;

		/** @brief WinRT のボタン 1 個と、境界の POD 側のビット 1 個の対応。 */
		struct ButtonMapping
		{
			winrt_input::GamepadButtons source;
			EnGamepadButton             destination;
		};

		// 並びは今のところ WinRT 側と同じだが、他所が決めた値の順序に寄りかからないよう表で持つ。
		constexpr ButtonMapping BUTTON_MAPPINGS[] = {
			{ winrt_input::GamepadButtons::Menu, EnGamepadButton::Menu },
			{ winrt_input::GamepadButtons::View, EnGamepadButton::View },
			{ winrt_input::GamepadButtons::A, EnGamepadButton::A },
			{ winrt_input::GamepadButtons::B, EnGamepadButton::B },
			{ winrt_input::GamepadButtons::X, EnGamepadButton::X },
			{ winrt_input::GamepadButtons::Y, EnGamepadButton::Y },
			{ winrt_input::GamepadButtons::DPadUp, EnGamepadButton::DPadUp },
			{ winrt_input::GamepadButtons::DPadDown, EnGamepadButton::DPadDown },
			{ winrt_input::GamepadButtons::DPadLeft, EnGamepadButton::DPadLeft },
			{ winrt_input::GamepadButtons::DPadRight, EnGamepadButton::DPadRight },
			{ winrt_input::GamepadButtons::LeftShoulder, EnGamepadButton::LeftShoulder },
			{ winrt_input::GamepadButtons::RightShoulder, EnGamepadButton::RightShoulder },
			{ winrt_input::GamepadButtons::LeftThumbstick, EnGamepadButton::LeftThumbstick },
			{ winrt_input::GamepadButtons::RightThumbstick, EnGamepadButton::RightThumbstick },
		};

		// 繋がっている間は掴んだ 1 台を持ち続け、毎フレームは読み取りだけ頼む。
		// ➡定常状態では 1 フレームあたりのヒープ確保が 0 になる。
		winrt_input::Gamepad s_cachedGamepad{ nullptr };

		std::chrono::steady_clock::time_point s_lastSearchTime{};

		bool s_hasSearchedOnce = false;

		/**
		 * @brief 未接続のときの探し直しの間合いを計る。
		 * @return 探してよければ true。true を返したときだけ次の間合いを数え直す。
		 */
		bool ShouldSearchGamepad()
		{
			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

			if (s_hasSearchedOnce)
			{
				const float elapsedSeconds = std::chrono::duration<float>(now - s_lastSearchTime).count();
				if (elapsedSeconds < GAMEPAD_SEARCH_INTERVAL_IN_SECONDS)
				{
					return false;
				}
			}

			s_lastSearchTime  = now;
			s_hasSearchedOnce = true;

			return true;
		}


		/** @brief 掴んでいるパッドが無ければ探す。掴めたか掴んでいるなら true。 */
		bool AcquireGamepad()
		{
			if (s_cachedGamepad != nullptr)
			{
				return true;
			}

			if (!ShouldSearchGamepad())
			{
				return false;
			}

			const auto gamepads = winrt_input::Gamepad::Gamepads();
			if (gamepads.Size() == 0)
			{
				return false;
			}

			// エディタを触るのは 1 人なので先頭の 1 台に決め打ちする。
			s_cachedGamepad = gamepads.GetAt(0);

			return true;
		}


		/** @brief WinRT のボタンのビットを境界の POD 側のビットに置き換える。 */
		uint32_t ToButtonBits(winrt_input::GamepadButtons buttons)
		{
			uint32_t bits = 0;

			for (const ButtonMapping& mapping : BUTTON_MAPPINGS)
			{
				if ((buttons & mapping.source) != winrt_input::GamepadButtons::None)
				{
					bits |= static_cast<uint32_t>(mapping.destination);
				}
			}

			return bits;
		}
	} // namespace


	GamepadState ReadGamepadState()
	{
		GamepadState state;

		try
		{
			if (!AcquireGamepad())
			{
				return state;
			}

			const winrt_input::GamepadReading reading = s_cachedGamepad.GetCurrentReading();

			state.isConnected = true;
			state.buttons     = ToButtonBits(reading.Buttons);

			state.leftStickX  = static_cast<float>(reading.LeftThumbstickX);
			state.leftStickY  = static_cast<float>(reading.LeftThumbstickY);
			state.rightStickX = static_cast<float>(reading.RightThumbstickX);
			state.rightStickY = static_cast<float>(reading.RightThumbstickY);

			state.leftTrigger  = static_cast<float>(reading.LeftTrigger);
			state.rightTrigger = static_cast<float>(reading.RightTrigger);

			return state;
		}
		catch (const winrt::hresult_error&)
		{
			// 抜かれた等。掴んでいたものを捨てて探し直しに戻す。
			// ログは出さない。毎フレーム走るので、失敗が続くと出力窓が埋まる。
			s_cachedGamepad = nullptr;

			return GamepadState{};
		}
	}


	bool IsButtonDown(const GamepadState& state, EnGamepadButton button)
	{
		return (state.buttons & static_cast<uint32_t>(button)) != 0;
	}
} // namespace fang::editor
