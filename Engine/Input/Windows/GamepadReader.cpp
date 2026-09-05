/**
 * @file GamepadReader.cpp
 * @brief XInput からパッドを 1 台読む実装（Windows）。
 */
#include "Pch.h"
#include "Input/Gamepad.h"
#include "Input/InputLog.h"
#include <windows.h>
#include <Xinput.h>
#include <chrono>


namespace fang
{
	namespace
	{
		/** @brief XInput が扱えるスロットの数。 */
		constexpr DWORD SLOT_COUNT = XUSER_MAX_COUNT;

		/** @brief 掴んでいないことを表すスロット番号。 */
		constexpr DWORD INVALID_SLOT = SLOT_COUNT;

		/**
		 * @brief 未接続のときにパッドを探し直す間隔（秒）。
		 * @details 繋がっていないスロットへの XInputGetState は重い、と公式が明示している
		 *          ➡ 見つからないうちは毎フレーム 4 スロットを叩かず、この間隔まで待つ。
		 */
		constexpr float SEARCH_INTERVAL_IN_SECONDS = 1.0f;

		/** @brief スティックの生値を -1 〜 1 に均すための除数。SHORT の正側の最大値。 */
		constexpr float STICK_DIVISOR = 32767.0f;

		/** @brief トリガーの生値を 0 〜 1 に均すための除数。BYTE の最大値。 */
		constexpr float TRIGGER_DIVISOR = 255.0f;

		/** @brief XInput のボタン 1 個と、境界の POD 側のビット 1 個の対応。 */
		struct ButtonMapping
		{
			WORD            source;
			EnGamepadButton destination;
		};

		// XInput は Start / Back という名前だが、境界では Xbox 本体の呼び方（Menu / View）にそろえる。
		constexpr ButtonMapping BUTTON_MAPPINGS[] = {
			{ XINPUT_GAMEPAD_START, EnGamepadButton::Menu },
			{ XINPUT_GAMEPAD_BACK, EnGamepadButton::View },
			{ XINPUT_GAMEPAD_A, EnGamepadButton::A },
			{ XINPUT_GAMEPAD_B, EnGamepadButton::B },
			{ XINPUT_GAMEPAD_X, EnGamepadButton::X },
			{ XINPUT_GAMEPAD_Y, EnGamepadButton::Y },
			{ XINPUT_GAMEPAD_DPAD_UP, EnGamepadButton::DPadUp },
			{ XINPUT_GAMEPAD_DPAD_DOWN, EnGamepadButton::DPadDown },
			{ XINPUT_GAMEPAD_DPAD_LEFT, EnGamepadButton::DPadLeft },
			{ XINPUT_GAMEPAD_DPAD_RIGHT, EnGamepadButton::DPadRight },
			{ XINPUT_GAMEPAD_LEFT_SHOULDER, EnGamepadButton::LeftShoulder },
			{ XINPUT_GAMEPAD_RIGHT_SHOULDER, EnGamepadButton::RightShoulder },
			{ XINPUT_GAMEPAD_LEFT_THUMB, EnGamepadButton::LeftThumbstick },
			{ XINPUT_GAMEPAD_RIGHT_THUMB, EnGamepadButton::RightThumbstick },
		};

		/** @brief 掴んでいるスロット。繋がっている間は探し直さない。 */
		DWORD s_acquiredSlot = INVALID_SLOT;

		std::chrono::steady_clock::time_point s_lastSearchTime{};

		bool s_hasSearchedOnce = false;

		/** @brief 掴めたことを 1 度だけ出したか。毎フレーム走るので出しっぱなしにしない。 */
		bool s_hasLoggedAcquired = false;

		/** @brief 見つからないことを 1 度だけ出したか。 */
		bool s_hasLoggedMissing = false;


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
				if (elapsedSeconds < SEARCH_INTERVAL_IN_SECONDS)
				{
					return false;
				}
			}

			s_lastSearchTime  = now;
			s_hasSearchedOnce = true;

			return true;
		}


		/**
		 * @brief 掴んでいるスロットが無ければ探す。
		 * @param outState 掴めたスロットの読み取り結果。
		 * @return 掴めていれば true。
		 */
		bool AcquireGamepad(XINPUT_STATE* outState)
		{
			if (s_acquiredSlot != INVALID_SLOT)
			{
				if (::XInputGetState(s_acquiredSlot, outState) == ERROR_SUCCESS)
				{
					return true;
				}

				// 抜かれた。次の間合いから探し直す。
				s_acquiredSlot     = INVALID_SLOT;
				s_hasSearchedOnce  = false;
				s_hasLoggedMissing = false;
			}

			if (!ShouldSearchGamepad())
			{
				return false;
			}

			for (DWORD slot = 0; slot < SLOT_COUNT; ++slot)
			{
				if (::XInputGetState(slot, outState) != ERROR_SUCCESS)
				{
					continue;
				}

				s_acquiredSlot = slot;

				if (!s_hasLoggedAcquired)
				{
					FANG_LOG_INFO(Input, "パッドを掴んだ (スロット {})", slot);
					s_hasLoggedAcquired = true;
				}

				return true;
			}

			if (!s_hasLoggedMissing)
			{
				FANG_LOG_INFO(Input, "パッドが見つからない。繋がるまで 1 秒おきに探す");
				s_hasLoggedMissing = true;
			}

			return false;
		}


		/** @brief XInput のボタンのビットを境界の POD 側のビットに置き換える。 */
		uint32_t ToButtonBits(WORD buttons)
		{
			uint32_t bits = 0;

			for (const ButtonMapping& mapping : BUTTON_MAPPINGS)
			{
				if ((buttons & mapping.source) != 0)
				{
					bits |= static_cast<uint32_t>(mapping.destination);
				}
			}

			return bits;
		}


		/** @brief スティックの軸を -1 〜 1 に均す。負側は 32768 まであるので、1 を超えないよう止める。 */
		float ToStickAxis(SHORT value)
		{
			const float normalized = static_cast<float>(value) / STICK_DIVISOR;

			return (normalized < -1.0f) ? -1.0f : normalized;
		}
	} // namespace


	GamepadState ReadGamepadState()
	{
		GamepadState state;

		XINPUT_STATE rawState{};
		if (!AcquireGamepad(&rawState))
		{
			return state;
		}

		const XINPUT_GAMEPAD& pad = rawState.Gamepad;

		state.isConnected = true;
		state.buttons     = ToButtonBits(pad.wButtons);

		state.leftStickX  = ToStickAxis(pad.sThumbLX);
		state.leftStickY  = ToStickAxis(pad.sThumbLY);
		state.rightStickX = ToStickAxis(pad.sThumbRX);
		state.rightStickY = ToStickAxis(pad.sThumbRY);

		state.leftTrigger  = static_cast<float>(pad.bLeftTrigger) / TRIGGER_DIVISOR;
		state.rightTrigger = static_cast<float>(pad.bRightTrigger) / TRIGGER_DIVISOR;

		return state;
	}
} // namespace fang
