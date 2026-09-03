/**
 * @file PackTests.cpp
 * @brief 頂点圧縮のスカラー変換のテスト。丸めの境界と往復誤差を既知の値で確かめる。
 */
#include "Core/Math/Pack.h"
#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <limits>


TEST_CASE("PackFloat16 は既知の値を既知のビット列にする")
{
	CHECK(fang::PackFloat16(0.0f) == 0x0000);
	CHECK(fang::PackFloat16(-0.0f) == 0x8000);
	CHECK(fang::PackFloat16(1.0f) == 0x3C00);
	CHECK(fang::PackFloat16(-1.0f) == 0xBC00);
	CHECK(fang::PackFloat16(0.5f) == 0x3800);

	// 1/2048。2048 テクセルのテクスチャで 1 テクセルに当たる刻み。
	CHECK(fang::PackFloat16(1.0f / 2048.0f) == 0x1000);
}


TEST_CASE("PackFloat16 は範囲の外を無限大へ飽和し、NaN を NaN のまま通す")
{
	// half の最大値 65504 はそのまま、それを超えると無限大。
	CHECK(fang::PackFloat16(65504.0f) == 0x7BFF);
	CHECK(fang::PackFloat16(100000.0f) == 0x7C00);
	CHECK(fang::PackFloat16(-100000.0f) == 0xFC00);

	const float infinity = std::numeric_limits<float>::infinity();
	CHECK(fang::PackFloat16(infinity) == 0x7C00);
	CHECK(std::isnan(fang::UnpackFloat16(fang::PackFloat16(std::numeric_limits<float>::quiet_NaN()))));
}


TEST_CASE("PackFloat16 は小さい値を 0 へ潰さず非正規化数にする")
{
	// 2^-24 は half の非正規化数の最小刻み。
	CHECK(fang::PackFloat16(std::ldexp(1.0f, -24)) == 0x0001);
	CHECK(fang::UnpackFloat16(0x0001) == doctest::Approx(std::ldexp(1.0f, -24)));

	// 2^-25 はちょうど 0 と最小刻みの中間。偶数タイで 0 に丸まる。
	CHECK(fang::PackFloat16(std::ldexp(1.0f, -25)) == 0x0000);

	// 非正規化数の途中の値も往復できる。
	const float subnormal = std::ldexp(3.0f, -24);
	CHECK(fang::UnpackFloat16(fang::PackFloat16(subnormal)) == doctest::Approx(subnormal));
}


TEST_CASE("PackFloat16 の往復誤差は half の刻みの半分に収まる")
{
	// [0.5, 1) の刻みは 1/2048 ➡ 誤差の上限はその半分。UV が 2048 テクスチャで 0.5 テクセルに相当する。
	constexpr float stepSize = 1.0f / 2048.0f;
	for (int i = 0; i < 64; ++i)
	{
		const float value    = 0.5f + static_cast<float>(i) * (stepSize / 4.0f);
		const float restored = fang::UnpackFloat16(fang::PackFloat16(value));
		CHECK(std::abs(restored - value) <= stepSize * 0.5f);
	}

	// タイリングで [0, 1] を出た UV も相対誤差 2^-11 以内で戻る。
	const float tiledValue    = 3.7f;
	const float tiledRestored = fang::UnpackFloat16(fang::PackFloat16(tiledValue));
	CHECK(std::abs(tiledRestored - tiledValue) <= tiledValue * (1.0f / 2048.0f));
}


TEST_CASE("PackSignedNormalized8 は端と 0 を正確に写す")
{
	CHECK(fang::PackSignedNormalized8(0.0f) == 0);
	CHECK(fang::PackSignedNormalized8(1.0f) == 127);
	CHECK(fang::PackSignedNormalized8(-1.0f) == -127);

	// 範囲の外はクランプ。-128 は決して出ない。
	CHECK(fang::PackSignedNormalized8(2.0f) == 127);
	CHECK(fang::PackSignedNormalized8(-5.0f) == -127);
}


TEST_CASE("PackSignedNormalized8 は最近接へ丸める")
{
	// 0.5 × 127 = 63.5。タイは 0 から遠い側 ➡ 64。
	CHECK(fang::PackSignedNormalized8(0.5f) == 64);
	CHECK(fang::PackSignedNormalized8(-0.5f) == -64);

	// 刻み（1/127）の半分の手前と先で割れる。
	CHECK(fang::PackSignedNormalized8(0.4f / 127.0f) == 0);
	CHECK(fang::PackSignedNormalized8(0.6f / 127.0f) == 1);
}
