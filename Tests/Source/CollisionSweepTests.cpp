/**
 * @file CollisionSweepTests.cpp
 * @brief 保守的前進による掃引のテスト。3 つの相手・退化した入力・すり抜けの再現を確かめる。
 */
#include "Collision/Collision.h"
#include "Collision/CollisionSweep.h"
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


	/** @brief 潰れたカプセル(= 球)として動かす形を作る。 */
	fang::Capsule MakeMovingSphere(const fang::Vector3& center, float radius)
	{
		return fang::Capsule{ .pointA = center, .pointB = center, .radius = radius };
	}
} // namespace


TEST_CASE("球相手への掃引が正面から近づいて当たる")
{
	const fang::Capsule       movingShape = MakeMovingSphere(fang::Vector3{ -10.0f, 0.0f, 0.0f }, 2.0f);
	const fang::ColliderShape target      = fang::MakeColliderShape(fang::Sphere{ .center = {}, .radius = 2.0f });

	fang::SweepHit hit;
	CHECK(fang::SweepAgainstShape(movingShape, fang::Vector3{ 20.0f, 0.0f, 0.0f }, target, &hit));
	CHECK(hit.timeRatio == doctest::Approx(0.3f));
	CHECK(hit.normal.x == doctest::Approx(-1.0f));
}


TEST_CASE("カプセル相手への掃引が当たる")
{
	const fang::Capsule       movingShape = MakeMovingSphere(fang::Vector3{ -10.0f, 0.0f, 0.0f }, 1.0f);
	const fang::ColliderShape target      = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { 0.0f, -5.0f, 0.0f }, .pointB = { 0.0f, 5.0f, 0.0f }, .radius = 1.0f }
	);

	fang::SweepHit hit;
	CHECK(fang::SweepAgainstShape(movingShape, fang::Vector3{ 20.0f, 0.0f, 0.0f }, target, &hit));

	// 表面が触れるのは x = -2 ➡ 8 / 20。
	CHECK(hit.timeRatio == doctest::Approx(8.0f / 20.0f));
	CHECK(hit.normal.x == doctest::Approx(-1.0f));
}


TEST_CASE("OBB 相手への掃引が当たる")
{
	const fang::OBB box = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{ 1.0f, 1.0f, 1.0f });

	const fang::Capsule       movingShape = MakeMovingSphere(fang::Vector3{ -10.0f, 0.0f, 0.0f }, 1.0f);
	const fang::ColliderShape target      = fang::MakeColliderShape(box);

	fang::SweepHit hit;
	CHECK(fang::SweepAgainstShape(movingShape, fang::Vector3{ 20.0f, 0.0f, 0.0f }, target, &hit));

	// 表面が触れるのは x = -2 ➡ 8 / 20。
	CHECK(hit.timeRatio == doctest::Approx(8.0f / 20.0f));
	CHECK(hit.normal.x == doctest::Approx(-1.0f));
}


TEST_CASE("動かない掃引は始点で重なっていれば当たり、離れていれば当たらない")
{
	const fang::ColliderShape target = fang::MakeColliderShape(fang::Sphere{ .center = {}, .radius = 2.0f });

	fang::SweepHit hit;

	// 始点で重なっている。
	CHECK(
		fang::SweepAgainstShape(
			MakeMovingSphere(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 2.0f),
			fang::Vector3{},
			target,
			&hit
		)
	);
	CHECK(hit.timeRatio == doctest::Approx(0.0f));

	// 離れていて、動きも無い。
	CHECK_FALSE(
		fang::SweepAgainstShape(
			MakeMovingSphere(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 2.0f),
			fang::Vector3{},
			target,
			&hit
		)
	);
}


TEST_CASE("退化した入力でも掃引が有限の結果になる")
{
	// 大きさ 0 の OBB へ、長さ 0 のカプセル(= 球)を動かす。
	const fang::OBB           flatBox     = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{});
	const fang::ColliderShape target      = fang::MakeColliderShape(flatBox);
	const fang::Capsule       movingShape = MakeMovingSphere(fang::Vector3{ -5.0f, 0.0f, 0.0f }, 1.0f);

	fang::SweepHit hit;
	const bool     hasHit = fang::SweepAgainstShape(movingShape, fang::Vector3{ 10.0f, 0.0f, 0.0f }, target, &hit);

	CHECK(std::isfinite(hit.timeRatio));
	if (hasHit)
	{
		CHECK(fang::Length(hit.normal) == doctest::Approx(1.0f));
	}
}


TEST_CASE("移動量が直径を超えるすり抜けを掃引だけが捉える")
{
	const fang::Sphere stationary{ .center = {}, .radius = 20.0f };

	const fang::Sphere movingAtStart{ .center = { -100.0f, 0.0f, 0.0f }, .radius = 20.0f };
	const fang::Sphere movingAtEnd{ .center = { 100.0f, 0.0f, 0.0f }, .radius = 20.0f };

	fang::Contact contact;
	CHECK_FALSE(fang::Intersect(movingAtStart, stationary, &contact));
	CHECK_FALSE(fang::Intersect(movingAtEnd, stationary, &contact));

	const fang::Capsule       movingShape = MakeMovingSphere(fang::Vector3{ -100.0f, 0.0f, 0.0f }, 20.0f);
	const fang::ColliderShape target      = fang::MakeColliderShape(stationary);

	fang::SweepHit hit;
	CHECK(fang::SweepAgainstShape(movingShape, fang::Vector3{ 200.0f, 0.0f, 0.0f }, target, &hit));
	CHECK(hit.timeRatio == doctest::Approx(0.3f));
}
