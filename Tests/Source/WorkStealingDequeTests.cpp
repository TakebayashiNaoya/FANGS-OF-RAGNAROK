/**
 * @file WorkStealingDequeTests.cpp
 * @brief Chase–Lev の両端キューのテスト。単独の出し入れと、持ち主 + 奪う側の同時実行。
 */
#include "Core/Job/WorkStealingDeque.h"
#include <doctest.h>
#include <atomic>
#include <thread>
#include <vector>


namespace
{
	constexpr uint32_t TEST_DEQUE_CAPACITY = 64;

	using TestDeque = fang::WorkStealingDeque<uint32_t, TEST_DEQUE_CAPACITY>;
} // namespace


TEST_CASE("持ち主は後入れ先出しで取り出す")
{
	TestDeque deque;

	for (uint32_t i = 0; i < 8; ++i)
	{
		CHECK(deque.Push(i));
	}

	CHECK(deque.GetCount() == 8);

	bool isOrderCorrect = true;
	for (uint32_t i = 8; i > 0; --i)
	{
		uint32_t value = 0xFFFFFFFFu;
		isOrderCorrect = isOrderCorrect && deque.Pop(value) && value == i - 1;
	}

	CHECK(isOrderCorrect);
	CHECK(deque.GetCount() == 0);
}

TEST_CASE("空のキューから取ろうとしても壊れない")
{
	TestDeque deque;

	uint32_t value = 0xFFFFFFFFu;
	CHECK_FALSE(deque.Pop(value));
	CHECK(deque.Steal(value) == fang::EnStealResult::Empty);

	// 空で Pop した後も、続けて出し入れできる。
	CHECK(deque.Push(42));
	CHECK(deque.Pop(value));
	CHECK(value == 42);
}

TEST_CASE("満杯になったら Push が false を返す")
{
	TestDeque deque;

	for (uint32_t i = 0; i < TEST_DEQUE_CAPACITY; ++i)
	{
		CHECK(deque.Push(i));
	}

	CHECK_FALSE(deque.Push(TEST_DEQUE_CAPACITY));

	uint32_t value = 0;
	CHECK(deque.Pop(value));
	CHECK(deque.Push(TEST_DEQUE_CAPACITY));
}

TEST_CASE("奪う側は先入れ先出しで取る")
{
	TestDeque deque;

	for (uint32_t i = 0; i < 4; ++i)
	{
		CHECK(deque.Push(i));
	}

	bool isOrderCorrect = true;
	for (uint32_t i = 0; i < 4; ++i)
	{
		uint32_t value = 0xFFFFFFFFu;
		isOrderCorrect = isOrderCorrect && deque.Steal(value) == fang::EnStealResult::Success && value == i;
	}

	CHECK(isOrderCorrect);
}

TEST_CASE("持ち主と奪う側が同時に触っても重複も欠落もしない")
{
	constexpr uint32_t ITEM_COUNT    = 50000;
	constexpr uint32_t STEALER_COUNT = 3;

	TestDeque deque;

	// 何回取れたかを要素ごとに数える。全部ちょうど 1 回でなければ、重複か欠落が起きている。
	std::vector<std::atomic<uint32_t>> takenCounts(ITEM_COUNT);
	for (auto& takenCount : takenCounts)
	{
		takenCount.store(0, std::memory_order_relaxed);
	}

	std::atomic<uint32_t> totalTakenCount{ 0 };

	std::vector<std::thread> stealers;
	stealers.reserve(STEALER_COUNT);
	for (uint32_t i = 0; i < STEALER_COUNT; ++i)
	{
		stealers.emplace_back([&] {
			while (totalTakenCount.load(std::memory_order_acquire) < ITEM_COUNT)
			{
				uint32_t value = 0;
				if (deque.Steal(value) == fang::EnStealResult::Success)
				{
					takenCounts[value].fetch_add(1, std::memory_order_relaxed);
					totalTakenCount.fetch_add(1, std::memory_order_release);
				}
				else
				{
					std::this_thread::yield();
				}
			}
		});
	}

	// 持ち主。満杯になったら自分でも消化して空きを作る。
	for (uint32_t i = 0; i < ITEM_COUNT; ++i)
	{
		while (!deque.Push(i))
		{
			uint32_t value = 0;
			if (deque.Pop(value))
			{
				takenCounts[value].fetch_add(1, std::memory_order_relaxed);
				totalTakenCount.fetch_add(1, std::memory_order_release);
			}
			else
			{
				std::this_thread::yield();
			}
		}
	}

	while (totalTakenCount.load(std::memory_order_acquire) < ITEM_COUNT)
	{
		uint32_t value = 0;
		if (deque.Pop(value))
		{
			takenCounts[value].fetch_add(1, std::memory_order_relaxed);
			totalTakenCount.fetch_add(1, std::memory_order_release);
		}
		else
		{
			std::this_thread::yield();
		}
	}

	for (std::thread& stealer : stealers)
	{
		stealer.join();
	}

	uint32_t wrongCount = 0;
	for (const auto& takenCount : takenCounts)
	{
		if (takenCount.load(std::memory_order_relaxed) != 1)
		{
			++wrongCount;
		}
	}

	CHECK(totalTakenCount.load(std::memory_order_relaxed) == ITEM_COUNT);
	CHECK(wrongCount == 0);
	CHECK(deque.GetCount() == 0);
}
