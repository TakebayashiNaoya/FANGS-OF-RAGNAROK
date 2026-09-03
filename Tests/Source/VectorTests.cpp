/**
 * @file VectorTests.cpp
 * @brief ベクトルのテスト。演算子と Dot / Cross / Length / Normalize を確かめる。
 */
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include <doctest.h>


TEST_CASE("Vector3 の加減算は成分ごとに計算する")
{
	const fang::Vector3 left{ 1.0f, 2.0f, 3.0f };
	const fang::Vector3 right{ 10.0f, 20.0f, 30.0f };

	const fang::Vector3 sum = left + right;
	CHECK(sum.x == doctest::Approx(11.0f));
	CHECK(sum.y == doctest::Approx(22.0f));
	CHECK(sum.z == doctest::Approx(33.0f));

	const fang::Vector3 difference = right - left;
	CHECK(difference.x == doctest::Approx(9.0f));
	CHECK(difference.y == doctest::Approx(18.0f));
	CHECK(difference.z == doctest::Approx(27.0f));
}


TEST_CASE("Vector3 の単項マイナスは全成分の符号を反転する")
{
	const fang::Vector3 value{ 1.0f, -2.0f, 3.0f };
	const fang::Vector3 negated = -value;

	CHECK(negated.x == doctest::Approx(-1.0f));
	CHECK(negated.y == doctest::Approx(2.0f));
	CHECK(negated.z == doctest::Approx(-3.0f));
}


TEST_CASE("Vector3 のスカラー倍はどちらの並びでも同じ結果になる")
{
	const fang::Vector3 value{ 1.0f, -2.0f, 3.0f };

	const fang::Vector3 scaledRight = value * 2.0f;
	const fang::Vector3 scaledLeft  = 2.0f * value;

	CHECK(scaledRight.x == doctest::Approx(2.0f));
	CHECK(scaledRight.y == doctest::Approx(-4.0f));
	CHECK(scaledRight.z == doctest::Approx(6.0f));

	CHECK(scaledLeft.x == doctest::Approx(scaledRight.x));
	CHECK(scaledLeft.y == doctest::Approx(scaledRight.y));
	CHECK(scaledLeft.z == doctest::Approx(scaledRight.z));
}


TEST_CASE("Vector3 の += と -= は左辺を書き換える")
{
	fang::Vector3 value{ 1.0f, 2.0f, 3.0f };

	value += fang::Vector3{ 10.0f, 10.0f, 10.0f };
	CHECK(value.x == doctest::Approx(11.0f));
	CHECK(value.y == doctest::Approx(12.0f));
	CHECK(value.z == doctest::Approx(13.0f));

	value -= fang::Vector3{ 1.0f, 2.0f, 3.0f };
	CHECK(value.x == doctest::Approx(10.0f));
	CHECK(value.y == doctest::Approx(10.0f));
	CHECK(value.z == doctest::Approx(10.0f));
}


TEST_CASE("Vector3 の Dot は直交する軸どうしで 0 になる")
{
	CHECK(fang::Dot(fang::Vector3{ 1.0f, 0.0f, 0.0f }, fang::Vector3{ 0.0f, 1.0f, 0.0f }) == doctest::Approx(0.0f));
	CHECK(fang::Dot(fang::Vector3{ 2.0f, 3.0f, 4.0f }, fang::Vector3{ 5.0f, 6.0f, 7.0f }) == doctest::Approx(56.0f));
}


TEST_CASE("Vector3 の Cross は +X と +Y から +Z を作り、両方に直交する")
{
	const fang::Vector3 axisX = { 1.0f, 0.0f, 0.0f };
	const fang::Vector3 axisY = { 0.0f, 1.0f, 0.0f };

	const fang::Vector3 cross = fang::Cross(axisX, axisY);
	CHECK(cross.x == doctest::Approx(0.0f));
	CHECK(cross.y == doctest::Approx(0.0f));
	CHECK(cross.z == doctest::Approx(1.0f));

	// 外積の結果は、元の 2 本のどちらとも内積が 0 になる（直交する）はず。
	const fang::Vector3 arbitraryLeft{ 2.0f, -1.0f, 3.0f };
	const fang::Vector3 arbitraryRight{ -4.0f, 5.0f, 1.0f };
	const fang::Vector3 arbitraryCross = fang::Cross(arbitraryLeft, arbitraryRight);
	CHECK(fang::Dot(arbitraryCross, arbitraryLeft) == doctest::Approx(0.0f));
	CHECK(fang::Dot(arbitraryCross, arbitraryRight) == doctest::Approx(0.0f));
}


TEST_CASE("Vector3 の LengthSquared と Length は 3-4-5 の直角三角形で確かめられる")
{
	const fang::Vector3 value{ 3.0f, 4.0f, 0.0f };

	CHECK(fang::LengthSquared(value) == doctest::Approx(25.0f));
	CHECK(fang::Length(value) == doctest::Approx(5.0f));
}


TEST_CASE("Vector3 の Normalize は長さを 1 にして向きを保つ")
{
	const fang::Vector3 value{ 3.0f, 4.0f, 0.0f };
	const fang::Vector3 normalized = fang::Normalize(value);

	CHECK(fang::Length(normalized) == doctest::Approx(1.0f));

	// 向きが保たれていれば、元のベクトルの定数倍（1 / 長さ）になっている。
	CHECK(normalized.x == doctest::Approx(0.6f));
	CHECK(normalized.y == doctest::Approx(0.8f));
	CHECK(normalized.z == doctest::Approx(0.0f));
}


TEST_CASE("Vector4 の加減算とスカラー倍は成分ごとに計算する")
{
	const fang::Vector4 left{ 1.0f, 2.0f, 3.0f, 4.0f };
	const fang::Vector4 right{ 10.0f, 20.0f, 30.0f, 40.0f };

	const fang::Vector4 sum = left + right;
	CHECK(sum.x == doctest::Approx(11.0f));
	CHECK(sum.y == doctest::Approx(22.0f));
	CHECK(sum.z == doctest::Approx(33.0f));
	CHECK(sum.w == doctest::Approx(44.0f));

	const fang::Vector4 difference = right - left;
	CHECK(difference.x == doctest::Approx(9.0f));
	CHECK(difference.y == doctest::Approx(18.0f));
	CHECK(difference.z == doctest::Approx(27.0f));
	CHECK(difference.w == doctest::Approx(36.0f));

	const fang::Vector4 scaled = left * 2.0f;
	CHECK(scaled.x == doctest::Approx(2.0f));
	CHECK(scaled.y == doctest::Approx(4.0f));
	CHECK(scaled.z == doctest::Approx(6.0f));
	CHECK(scaled.w == doctest::Approx(8.0f));
}


TEST_CASE("Vector4 の Dot は w を含めた 4 成分で計算する")
{
	// 点を w = 1 の同次座標にして平面と Dot すると、符号付き距離（ax + by + cz + d）と同じ形になる。
	// Frustum の交差判定はこの形を使っている。
	const fang::Vector4 plane{ 1.0f, 0.0f, 0.0f, -5.0f };
	const fang::Vector4 point{ 8.0f, 3.0f, 3.0f, 1.0f };

	CHECK(fang::Dot(plane, point) == doctest::Approx(3.0f));
}
