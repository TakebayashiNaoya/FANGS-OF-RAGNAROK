/**
 * @file CoreMacrosTests.cpp
 * @brief 基本マクロのテスト。要素数の数え方と、未使用の引数の捨て方を確かめる。
 */
#include "Core/CoreMacros.h"
#include <doctest.h>
#include <cstdint>


TEST_CASE("FANG_COUNT_OF は配列の要素数を返す")
{
	const int values[] = { 1, 2, 3, 4, 5 };
	CHECK(FANG_COUNT_OF(values) == 5);

	// 要素の大きさに引きずられない。sizeof の比なので 1 要素が何バイトでも個数が出る。
	const std::uint64_t wideValues[] = { 1, 2, 3 };
	CHECK(FANG_COUNT_OF(wideValues) == 3);

	const char text[] = "abc";
	CHECK(FANG_COUNT_OF(text) == 4); // 終端の null を含む。
}


TEST_CASE("FANG_COUNT_OF は定数式なので配列の宣言に使える")
{
	const int source[] = { 10, 20, 30 };
	int       copy[FANG_COUNT_OF(source)]{};

	static_assert(FANG_COUNT_OF(source) == 3);
	CHECK(FANG_COUNT_OF(copy) == FANG_COUNT_OF(source));
}


TEST_CASE("FANG_UNUSED は値を捨てるだけで副作用を起こさない")
{
	int counter = 0;

	// 引数は 1 度だけ評価される。マクロが引数を 2 回書いていないことの確認。
	FANG_UNUSED(++counter);
	CHECK(counter == 1);

	const int value = 42;
	FANG_UNUSED(value);
	CHECK(value == 42);
}
