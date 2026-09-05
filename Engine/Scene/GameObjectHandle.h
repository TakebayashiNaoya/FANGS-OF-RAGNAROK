/**
 * @file GameObjectHandle.h
 * @brief Scene 上のオブジェクトを指す、世代付きのハンドル。
 */
#pragma once

#include <cstdint>


namespace fang
{
	/**
	 * @brief Scene::CreateObject が返す、オブジェクト 1 個への参照。
	 * @details index はスロット配列上の位置、generation はそのスロットが再利用された回数。
	 *          破棄すると generation が 1 進むので、古いハンドルは同じ席に入った別のオブジェクトに当たらない。
	 */
	struct GameObjectHandle
	{
		static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

		uint32_t index      = INVALID_INDEX; /**< スロット配列上の位置。 */
		uint32_t generation = 0;             /**< スロットが再利用された回数。 */

		/** @brief 既定構築と、上限超過で返る無効なハンドルなら false。 */
		[[nodiscard]] constexpr bool IsValid() const { return index != INVALID_INDEX; }

		[[nodiscard]] constexpr bool operator==(const GameObjectHandle& other) const
		{
			return index == other.index && generation == other.generation;
		}

		[[nodiscard]] constexpr bool operator!=(const GameObjectHandle& other) const { return !(*this == other); }
	};
} // namespace fang
