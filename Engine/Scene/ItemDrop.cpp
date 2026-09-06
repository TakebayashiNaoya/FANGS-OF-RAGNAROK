/**
 * @file ItemDrop.cpp
 * @brief 撃破からアイテムのドロップ・拾い・寿命までの、状態を持たない計算。
 */
#include "Pch.h"
#include "Scene/ItemDrop.h"


namespace fang
{
	namespace
	{
		/** @brief 1 フレームの引き算の丸め残り（1/167 秒）。これ以下は 0 へ落とす（TickInvincibility と同じ）。 */
		constexpr float LIFETIME_EPSILON_SECONDS = 1.0e-4f;
	} // namespace


	uint32_t HashInteger(uint32_t value)
	{
		value ^= value >> 16;
		value *= 0x85ebca6bu;
		value ^= value >> 13;
		value *= 0xc2b2ae35u;
		value ^= value >> 16;
		return value;
	}


	float ComputeDropRatio(uint32_t serialNumber)
	{
		// 上位 24 ビット / 2^24。float の仮数は 24 ビットなので、この商は丸めずに表せる ➡ 必ず 1 未満。
		return static_cast<float>(HashInteger(serialNumber) >> 8) * (1.0f / 16777216.0f);
	}


	bool ShouldDropItem(const ItemDropParameter& parameter, uint32_t defeatSerialNumber)
	{
		return ComputeDropRatio(defeatSerialNumber) < parameter.dropProbability;
	}


	bool AddItemToBag(const ItemDropParameter& parameter, ItemBag* bag)
	{
		if (bag->count >= parameter.bagCapacity)
		{
			return false;
		}

		++bag->count;
		return true;
	}


	bool TakeItemFromBag(ItemBag* bag)
	{
		if (bag->count <= 0)
		{
			return false;
		}

		--bag->count;
		return true;
	}


	uint32_t StepItemLifetimes(std::span<float> remainingSecondsPerSlot, float deltaTimeSeconds)
	{
		uint32_t expiredSlotMask = 0;

		for (size_t slotIndex = 0; slotIndex < remainingSecondsPerSlot.size(); ++slotIndex)
		{
			float& remainingSeconds = remainingSecondsPerSlot[slotIndex];
			if (remainingSeconds <= 0.0f)
			{
				continue;
			}

			remainingSeconds -= deltaTimeSeconds;
			if (remainingSeconds <= LIFETIME_EPSILON_SECONDS)
			{
				remainingSeconds = 0.0f;
				expiredSlotMask |= (1u << slotIndex);
			}
		}

		return expiredSlotMask;
	}


	uint32_t SelectItemSlot(std::span<const float> remainingSecondsPerSlot)
	{
		uint32_t oldestSlotIndex        = static_cast<uint32_t>(remainingSecondsPerSlot.size());
		float    oldestRemainingSeconds = 0.0f;

		for (size_t slotIndex = 0; slotIndex < remainingSecondsPerSlot.size(); ++slotIndex)
		{
			const float remainingSeconds = remainingSecondsPerSlot[slotIndex];
			if (remainingSeconds <= 0.0f)
			{
				return static_cast<uint32_t>(slotIndex);
			}

			if (oldestSlotIndex == remainingSecondsPerSlot.size() || remainingSeconds < oldestRemainingSeconds)
			{
				oldestSlotIndex        = static_cast<uint32_t>(slotIndex);
				oldestRemainingSeconds = remainingSeconds;
			}
		}

		return oldestSlotIndex;
	}


	bool IsWithinPickupRange(
		const ItemDropParameter& parameter,
		const Vector3&           itemPosition,
		const Vector3&           collectorPosition
	)
	{
		const float radius = parameter.pickupRadiusCentimeters;
		return LengthSquared(itemPosition - collectorPosition) <= radius * radius;
	}
} // namespace fang
