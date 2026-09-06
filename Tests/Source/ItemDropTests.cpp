/**
 * @file ItemDropTests.cpp
 * @brief ItemDrop（ドロップ判定・並びの偏り・バッグの出し入れ・寿命・回収距離）のテスト。
 */
#include "Scene/ItemDrop.h"
#include <doctest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>


TEST_CASE("HashInteger: 0は不動点(通算番号を1から数える理由)")
{
	CHECK(fang::HashInteger(0) == 0);
}


TEST_CASE("ComputeDropRatio: 同じ番号には毎回同じ答えが出る")
{
	CHECK(fang::ComputeDropRatio(42) == fang::ComputeDropRatio(42));
	CHECK(fang::ComputeDropRatio(1) == fang::ComputeDropRatio(1));
}


TEST_CASE("ComputeDropRatio: 0以上1未満に収まる")
{
	for (uint32_t serialNumber = 1; serialNumber <= 1000; ++serialNumber)
	{
		const float ratio = fang::ComputeDropRatio(serialNumber);
		CHECK(ratio >= 0.0f);
		CHECK(ratio < 1.0f);
	}
}


TEST_CASE("ShouldDropItem: 率0では1000体回しても1個も落ちない")
{
	fang::ItemDropParameter parameter;
	parameter.dropProbability = 0.0f;

	uint32_t dropCount = 0;
	for (uint32_t serialNumber = 1; serialNumber <= 1000; ++serialNumber)
	{
		if (fang::ShouldDropItem(parameter, serialNumber))
		{
			++dropCount;
		}
	}
	CHECK(dropCount == 0);
}


TEST_CASE("ShouldDropItem: 率1では1000体回すと必ず落ちる")
{
	fang::ItemDropParameter parameter;
	parameter.dropProbability = 1.0f;

	uint32_t dropCount = 0;
	for (uint32_t serialNumber = 1; serialNumber <= 1000; ++serialNumber)
	{
		if (fang::ShouldDropItem(parameter, serialNumber))
		{
			++dropCount;
		}
	}
	CHECK(dropCount == 1000);
}


TEST_CASE("ShouldDropItem: 率0.1で1000体回すと85〜115回に収まる")
{
	fang::ItemDropParameter parameter;
	parameter.dropProbability = 0.1f;

	uint32_t dropCount = 0;
	for (uint32_t serialNumber = 1; serialNumber <= 1000; ++serialNumber)
	{
		if (fang::ShouldDropItem(parameter, serialNumber))
		{
			++dropCount;
		}
	}
	CHECK(dropCount >= 85);
	CHECK(dropCount <= 115);
}


TEST_CASE("ShouldDropItem: 同じ通算番号を2回叩いても答えが一致する(合否が再現する)")
{
	fang::ItemDropParameter parameter;
	parameter.dropProbability = 0.1f;

	for (uint32_t serialNumber = 1; serialNumber <= 300; ++serialNumber)
	{
		CHECK(fang::ShouldDropItem(parameter, serialNumber) == fang::ShouldDropItem(parameter, serialNumber));
	}
}


TEST_CASE("ShouldDropItem: 落ちる並びの間隔が等間隔(10体ごと等)にならない")
{
	fang::ItemDropParameter parameter;
	parameter.dropProbability = 0.1f;

	std::vector<uint32_t> droppedSerialNumbers;
	for (uint32_t serialNumber = 1; serialNumber <= 300; ++serialNumber)
	{
		if (fang::ShouldDropItem(parameter, serialNumber))
		{
			droppedSerialNumbers.push_back(serialNumber);
		}
	}

	CHECK(droppedSerialNumbers.size() >= 2);

	std::vector<uint32_t> gaps;
	for (size_t index = 1; index < droppedSerialNumbers.size(); ++index)
	{
		gaps.push_back(droppedSerialNumbers[index] - droppedSerialNumbers[index - 1]);
	}

	// 全部の間隔が同じ値なら等間隔ということ。2 種類以上あれば不規則。
	const bool allGapsEqual = std::all_of(gaps.begin(), gaps.end(), [&](uint32_t gap) { return gap == gaps.front(); });
	CHECK_FALSE(allGapsEqual);
}


TEST_CASE("AddItemToBag: 上限未満なら個数が1増える")
{
	fang::ItemDropParameter parameter;
	parameter.bagCapacity = 5;

	fang::ItemBag bag;
	CHECK(fang::AddItemToBag(parameter, &bag));
	CHECK(bag.count == 1);
}


TEST_CASE("AddItemToBag: 上限に達していれば入らず個数も動かない")
{
	fang::ItemDropParameter parameter;
	parameter.bagCapacity = 5;

	fang::ItemBag bag{ .count = 5 };
	CHECK_FALSE(fang::AddItemToBag(parameter, &bag));
	CHECK(bag.count == 5);
}


TEST_CASE("TakeItemFromBag: 1個以上あれば1減る")
{
	fang::ItemBag bag{ .count = 3 };
	CHECK(fang::TakeItemFromBag(&bag));
	CHECK(bag.count == 2);
}


TEST_CASE("TakeItemFromBag: 0個なら取れず、個数も負にならない")
{
	fang::ItemBag bag{ .count = 0 };
	CHECK_FALSE(fang::TakeItemFromBag(&bag));
	CHECK(bag.count == 0);
}


TEST_CASE("StepItemLifetimes: 14.9秒では尽きず、15.0秒に達した瞬間にビットが立つ")
{
	std::array<float, 1> remainingSeconds{ 15.0f };

	uint32_t expiredMask = fang::StepItemLifetimes(remainingSeconds, 14.9f);
	CHECK(expiredMask == 0);
	CHECK(remainingSeconds[0] == doctest::Approx(0.1f));

	expiredMask = fang::StepItemLifetimes(remainingSeconds, 0.1f);
	CHECK(expiredMask == 0b1u);
	CHECK(remainingSeconds[0] == doctest::Approx(0.0f));
}


TEST_CASE("StepItemLifetimes: 空き席(0)は減らさず、ビットも立てない")
{
	std::array<float, 3> remainingSeconds{ 0.0f, 5.0f, 0.0f };

	const uint32_t expiredMask = fang::StepItemLifetimes(remainingSeconds, 1.0f);
	CHECK(expiredMask == 0);
	CHECK(remainingSeconds[0] == doctest::Approx(0.0f));
	CHECK(remainingSeconds[1] == doctest::Approx(4.0f));
	CHECK(remainingSeconds[2] == doctest::Approx(0.0f));
}


TEST_CASE("StepItemLifetimes: 複数席が同時に尽きたときはそれぞれのビットが立つ")
{
	std::array<float, 4> remainingSeconds{ 0.5f, 5.0f, 0.5f, 0.5f };

	const uint32_t expiredMask = fang::StepItemLifetimes(remainingSeconds, 1.0f);
	CHECK(expiredMask == 0b1101u);
	for (float value : remainingSeconds)
	{
		CHECK(value >= 0.0f);
	}
}


TEST_CASE("SelectItemSlot: 空き席があればその番号を返す")
{
	std::array<float, 4> remainingSeconds{ 3.0f, 0.0f, 5.0f, 1.0f };
	CHECK(fang::SelectItemSlot(remainingSeconds) == 1);
}


TEST_CASE("SelectItemSlot: 空き席が無ければ残りが最も少ない席(最も古い)を返す")
{
	std::array<float, 4> remainingSeconds{ 3.0f, 8.0f, 5.0f, 1.0f };
	CHECK(fang::SelectItemSlot(remainingSeconds) == 3);
}


TEST_CASE("SelectItemSlot: 空のspanならsize()を返す")
{
	const std::span<const float> empty;
	CHECK(fang::SelectItemSlot(empty) == 0);
}


TEST_CASE("IsWithinPickupRange: 200cm以内(境界含む)は入り、201cmは入らない")
{
	fang::ItemDropParameter parameter;
	parameter.pickupRadiusCentimeters = 200.0f;

	const fang::Vector3 collector{ 0.0f, 0.0f, 0.0f };

	CHECK(fang::IsWithinPickupRange(parameter, fang::Vector3{ 199.0f, 0.0f, 0.0f }, collector));
	CHECK(fang::IsWithinPickupRange(parameter, fang::Vector3{ 200.0f, 0.0f, 0.0f }, collector));
	CHECK_FALSE(fang::IsWithinPickupRange(parameter, fang::Vector3{ 201.0f, 0.0f, 0.0f }, collector));
}


TEST_CASE("ItemDrop: つまみが寿命0・上限0・割合0でも落ちず無限ループしない")
{
	fang::ItemDropParameter parameter;
	parameter.lifetimeSeconds = 0.0f;
	parameter.bagCapacity     = 0;
	parameter.healRatio       = 0.0f;

	fang::ItemBag bag;
	CHECK_FALSE(fang::AddItemToBag(parameter, &bag));
	CHECK(bag.count == 0);

	std::array<float, 8> remainingSeconds{};
	const uint32_t       expiredMask = fang::StepItemLifetimes(remainingSeconds, 1.0f / 60.0f);
	CHECK(expiredMask == 0); // 既に0の席はビットが立たない。
}


TEST_CASE("ItemDrop: 1000体ぶんの撃破と8席の出し入れを回してもクラッシュせず値が壊れない")
{
	// ItemDropParameter / ItemBag はどちらも int32_t と float だけの POD で、この 3 関数はどれも
	// アロケータを受け取らず可変長の入れ物も持たない ➡ 構造としてヒープを確保できない
	// (撃破1件・拾い1件あたりのヒープ確保が0、設計の品質ゲート)。
	fang::ItemDropParameter parameter;
	fang::ItemBag           bag;

	std::array<float, 8> remainingSeconds{};

	uint32_t dropCount = 0;
	for (uint32_t serialNumber = 1; serialNumber <= 1000; ++serialNumber)
	{
		if (fang::ShouldDropItem(parameter, serialNumber))
		{
			++dropCount;

			const uint32_t slotIndex = fang::SelectItemSlot(remainingSeconds);
			CHECK(slotIndex < remainingSeconds.size());
			remainingSeconds[slotIndex] = parameter.lifetimeSeconds;
		}

		(void)fang::StepItemLifetimes(remainingSeconds, 1.0f / 60.0f);

		if (fang::AddItemToBag(parameter, &bag))
		{
			CHECK(fang::TakeItemFromBag(&bag));
		}
	}

	CHECK(dropCount > 0);
	CHECK(bag.count == 0);
	CHECK(bag.count <= parameter.bagCapacity);
}
