/**
 * @file GamepadDestinationTests.cpp
 * @brief パッドの行き先切り替えのテスト。反転条件・持ち越しの潰し方・Filter の中身を確かめる。
 */
#if FANG_ENABLE_EDITOR

#include "Input/GamepadDestination.h"
#include <doctest.h>


namespace
{
	/** @brief 接続済みで、指定したボタンだけを下げた状態を作る。 */
	fang::GamepadState MakeGamepadState(uint32_t buttons = 0)
	{
		fang::GamepadState state;
		state.isConnected = true;
		state.buttons     = buttons;
		return state;
	}
} // namespace


TEST_CASE("既定はゲーム")
{
	const fang::GamepadDestination destination;
	CHECK(destination.Get() == fang::EnGamepadDestination::Game);
}


TEST_CASE("View の押し始めで反転する")
{
	fang::GamepadDestination destination;
	destination.Advance(MakeGamepadState(static_cast<uint32_t>(fang::EnGamepadButton::View)));

	CHECK(destination.Get() == fang::EnGamepadDestination::Editor);
	CHECK(destination.HasJustChanged());
}


TEST_CASE("押しっぱなしで往復しない")
{
	fang::GamepadDestination destination;
	const fang::GamepadState viewHeld = MakeGamepadState(static_cast<uint32_t>(fang::EnGamepadButton::View));

	for (int i = 0; i < 10; ++i)
	{
		destination.Advance(viewHeld);
	}

	CHECK(destination.Get() == fang::EnGamepadDestination::Editor);
}


TEST_CASE("離して押し直せば戻る")
{
	fang::GamepadDestination destination;
	const fang::GamepadState viewPressed  = MakeGamepadState(static_cast<uint32_t>(fang::EnGamepadButton::View));
	const fang::GamepadState viewReleased = MakeGamepadState();

	destination.Advance(viewPressed);
	CHECK(destination.Get() == fang::EnGamepadDestination::Editor);

	destination.Advance(viewReleased);
	destination.Advance(viewPressed);

	CHECK(destination.Get() == fang::EnGamepadDestination::Game);
}


TEST_CASE("未接続では変わらない")
{
	fang::GamepadDestination destination;

	fang::GamepadState state;
	state.isConnected = false;
	state.buttons     = static_cast<uint32_t>(fang::EnGamepadButton::View);
	destination.Advance(state);

	CHECK(destination.Get() == fang::EnGamepadDestination::Game);
	CHECK_FALSE(destination.HasJustChanged());
}


TEST_CASE("ゲーム側にいる間、ImGui へは isConnected 以外 0")
{
	const fang::GamepadDestination destination;

	fang::GamepadState state = MakeGamepadState(static_cast<uint32_t>(fang::EnGamepadButton::A));
	state.leftStickX         = 1.0f;

	const fang::GamepadState filtered = destination.FilterForEditor(state);

	CHECK(filtered.isConnected);
	CHECK(filtered.buttons == 0);
	CHECK(filtered.leftStickX == doctest::Approx(0.0f));
}


TEST_CASE("ImGui 側にいる間、ゲームへは isConnected 以外 0")
{
	fang::GamepadDestination destination;
	destination.Advance(MakeGamepadState(static_cast<uint32_t>(fang::EnGamepadButton::View)));

	fang::GamepadState state = MakeGamepadState(static_cast<uint32_t>(fang::EnGamepadButton::A));
	state.leftStickX         = 1.0f;

	const fang::GamepadState filtered = destination.FilterForGame(state);

	CHECK(filtered.isConnected);
	CHECK(filtered.buttons == 0);
	CHECK(filtered.leftStickX == doctest::Approx(0.0f));
}


TEST_CASE("切り替えた瞬間に押していたボタンは離すまで流れない")
{
	fang::GamepadDestination destination;

	constexpr uint32_t VIEW_BIT = static_cast<uint32_t>(fang::EnGamepadButton::View);
	constexpr uint32_t Y_BIT    = static_cast<uint32_t>(fang::EnGamepadButton::Y);

	destination.Advance(MakeGamepadState(VIEW_BIT | Y_BIT));
	CHECK(destination.Get() == fang::EnGamepadDestination::Editor);

	const fang::GamepadState yHeld = MakeGamepadState(Y_BIT);
	destination.Advance(yHeld);

	const fang::GamepadState filteredForEditor = destination.FilterForEditor(yHeld);
	const fang::GamepadState filteredForGame   = destination.FilterForGame(yHeld);

	CHECK((filteredForEditor.buttons & Y_BIT) == 0);
	CHECK((filteredForGame.buttons & Y_BIT) == 0);
}


TEST_CASE("離せば次から流れる")
{
	fang::GamepadDestination destination;

	constexpr uint32_t VIEW_BIT = static_cast<uint32_t>(fang::EnGamepadButton::View);
	constexpr uint32_t Y_BIT    = static_cast<uint32_t>(fang::EnGamepadButton::Y);

	destination.Advance(MakeGamepadState(VIEW_BIT | Y_BIT));
	destination.Advance(MakeGamepadState()); // 両方離す
	const fang::GamepadState yHeldAgain = MakeGamepadState(Y_BIT);
	destination.Advance(yHeldAgain);

	const fang::GamepadState filtered = destination.FilterForEditor(yHeldAgain);
	CHECK((filtered.buttons & Y_BIT) != 0);
}


TEST_CASE("スティックは持ち越さない")
{
	fang::GamepadDestination destination;

	constexpr uint32_t VIEW_BIT = static_cast<uint32_t>(fang::EnGamepadButton::View);
	constexpr uint32_t Y_BIT    = static_cast<uint32_t>(fang::EnGamepadButton::Y);

	destination.Advance(MakeGamepadState(VIEW_BIT | Y_BIT));

	fang::GamepadState yWithStick = MakeGamepadState(Y_BIT);
	yWithStick.leftStickX         = 1.0f;
	destination.Advance(yWithStick);

	const fang::GamepadState filtered = destination.FilterForEditor(yWithStick);

	CHECK((filtered.buttons & Y_BIT) == 0);
	CHECK(filtered.leftStickX == doctest::Approx(1.0f));
}

#endif // FANG_ENABLE_EDITOR
