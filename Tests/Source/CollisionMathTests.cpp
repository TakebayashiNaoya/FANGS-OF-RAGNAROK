/**
 * @file CollisionMathTests.cpp
 * @brief 芯の点と OBB の分離距離のテスト。外なら正、中なら負、退化した箱でも NaN を返さないことを見る。
 */
#include "Collision/CollisionMath.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 軸平行の OBB。 */
	fang::OBB MakeAxisAlignedBox(const fang::Vector3& center, const fang::Vector3& halfExtents)
	{
		return fang::OBB{ .center = center, .halfExtents = halfExtents };
	}
} // namespace


TEST_CASE("箱の外なら分離距離は正で面までの距離になる")
{
	const fang::OBB box = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{ 1.0f, 1.0f, 1.0f });

	const fang::CoreBoxSeparation separation = fang::ComputeCoreToBoxSeparation(fang::Vector3{ 3.0f, 0.0f, 0.0f }, box);

	CHECK(separation.distance == doctest::Approx(2.0f));
	CHECK(separation.normal.x == doctest::Approx(1.0f));
	CHECK(separation.closestPoint.x == doctest::Approx(1.0f));
}


TEST_CASE("箱の中なら分離距離は負でいちばん浅い面を向く")
{
	// X の半分は 1、Y の半分は 3 ➡ (0.5, 0, 0) はどちらの面にも近いが X のほうが浅い。
	const fang::OBB box = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{ 1.0f, 3.0f, 3.0f });

	const fang::CoreBoxSeparation separation = fang::ComputeCoreToBoxSeparation(fang::Vector3{ 0.5f, 0.0f, 0.0f }, box);

	CHECK(separation.distance == doctest::Approx(-0.5f));
	CHECK(separation.normal.x == doctest::Approx(1.0f));
	CHECK(separation.closestPoint.x == doctest::Approx(1.0f));
}


TEST_CASE("退化した大きさ 0 の箱でも分離距離が有限になる")
{
	const fang::OBB flat = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{});

	const fang::CoreBoxSeparation separation =
		fang::ComputeCoreToBoxSeparation(fang::Vector3{ 2.0f, 0.0f, 0.0f }, flat);

	CHECK(std::isfinite(separation.distance));
	CHECK(fang::Length(separation.normal) == doctest::Approx(1.0f));
}
