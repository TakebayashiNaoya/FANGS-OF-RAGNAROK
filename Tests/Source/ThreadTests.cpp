/**
 * @file ThreadTests.cpp
 * @brief 使えるコア数とスレッド名付けのテスト。
 */
#include "Core/Platform/Thread.h"
#include <doctest.h>
#include <cstdint>
#include <thread>


TEST_CASE("使えるコア数は 1 以上で、呼ぶたびに同じ値を返す")
{
	const uint32_t coreCount = fang::GetUsableCoreCount();

	CHECK(coreCount >= 1);
	CHECK(coreCount == fang::GetUsableCoreCount());

	// 論理プロセッサ数を超えることはない。超えたら数え方が壊れている。
	const uint32_t logicalCount = std::thread::hardware_concurrency();
	if (logicalCount > 0)
	{
		CHECK(coreCount <= logicalCount);
	}
}


TEST_CASE("スレッド名付けは nullptr を渡しても落ちない")
{
	fang::SetCurrentThreadName("FangThreadTest");
	fang::SetCurrentThreadName(nullptr);

	// 上限を超える長さでも切り捨てるだけで落ちない。
	fang::SetCurrentThreadName("FangThreadTestWithVeryLongNameThatExceedsTheLimit");
}
