/**
 * @file GamepadDestination.h
 * @brief パッドの行き先（ゲームか ImGui か）の状態と、切り替えの検出。
 * @details Release では FANG_ENABLE_EDITOR が 0 になり、この型はどの翻訳単位にも現れない。
 *          .cpp を置かないのは、Release で中身が空の翻訳単位になるのを避けるため。
 */
#pragma once

#if FANG_ENABLE_EDITOR

#include "Core/CoreMacros.h"
#include "Input/Gamepad.h"
#include <cstdint>


namespace fang
{
	/** @brief パッドの行き先。 */
	enum class EnGamepadDestination : uint8_t
	{
		Game,   /**< 既定。狼を動かす。 */
		Editor, /**< ImGui のナビゲーション。 */
	};

	/**
	 * @brief パッドの行き先を持つ、プロセスに 1 つの切り替え係。
	 * @details View の押し始めで行き先を反転する。切り替えた瞬間に押していたボタンは、物理的に
	 *          離れるまでどちらの Filter からも 0 として返る（押しっぱなしの持ち越しを潰すため）。
	 *          スティックは持ち越さない。倒したまま切り替えたなら本人がそう倒している。
	 * @threading メインスレッドのみ。書くのは描画中の EditorUI::BuildFrame、読むのは次の周の頭の
	 *            RunApplication。どちらもメインなので新しい同期は要らない。
	 */
	class GamepadDestination
	{
	public:
		[[nodiscard]] static GamepadDestination& GetInstance()
		{
			static GamepadDestination instance;
			return instance;
		}

		[[nodiscard]] FANG_FORCEINLINE EnGamepadDestination Get() const { return m_destination; }

		/** @brief 直前の Advance で行き先が変わったか。 */
		[[nodiscard]] FANG_FORCEINLINE bool HasJustChanged() const { return m_hasJustChanged; }


	public:
		/** @brief View の押し始めで行き先を反転し、押しっぱなしの持ち越しを畳む。 */
		void Advance(const GamepadState& state)
		{
			constexpr uint32_t VIEW_BUTTON_BIT = static_cast<uint32_t>(EnGamepadButton::View);

			const bool isViewDownNow        = state.isConnected && (state.buttons & VIEW_BUTTON_BIT) != 0;
			const bool isViewDownPreviously = (m_previousButtons & VIEW_BUTTON_BIT) != 0;
			const bool isPressStart         = isViewDownNow && !isViewDownPreviously;

			m_previousButtons = state.isConnected ? state.buttons : 0;
			m_carriedButtons &= state.buttons;

			m_hasJustChanged = isPressStart;
			if (isPressStart)
			{
				m_destination    = m_destination == EnGamepadDestination::Game ? EnGamepadDestination::Editor
																			   : EnGamepadDestination::Game;
				m_carriedButtons = state.buttons;
			}
		}

		/** @brief ImGui へ流してよい状態。行き先がゲームなら isConnected 以外は全部 0。 */
		[[nodiscard]] FANG_FORCEINLINE GamepadState FilterForEditor(const GamepadState& state) const
		{
			return Filter(state, EnGamepadDestination::Editor);
		}

		/** @brief ゲームへ渡してよい状態。行き先がエディタなら isConnected 以外は全部 0。 */
		[[nodiscard]] FANG_FORCEINLINE GamepadState FilterForGame(const GamepadState& state) const
		{
			return Filter(state, EnGamepadDestination::Game);
		}


	private:
		/** @brief 行き先が一致していなければ isConnected だけを残す。一致していれば持ち越し中のボタンを落とす。 */
		[[nodiscard]] GamepadState Filter(const GamepadState& state, EnGamepadDestination destination) const
		{
			if (m_destination != destination)
			{
				return GamepadState{ .isConnected = state.isConnected };
			}

			GamepadState filtered = state;
			filtered.buttons &= ~m_carriedButtons;
			return filtered;
		}


	private:
		EnGamepadDestination m_destination = EnGamepadDestination::Game;

		uint32_t m_previousButtons = 0; /**< 前フレームの Advance が見たボタン。押し始めを出す。 */
		uint32_t m_carriedButtons  = 0; /**< 切り替えた瞬間に押していたボタン。離すまで流さない。 */

		bool m_hasJustChanged = false;
	};
} // namespace fang

#endif // FANG_ENABLE_EDITOR
