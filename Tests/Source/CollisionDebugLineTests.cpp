/**
 * @file CollisionDebugLineTests.cpp
 * @brief コライダーのワイヤーフレームのテスト。本数と、線が形の上に乗っていることを確かめる。
 */
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>
#include <vector>


namespace
{
	/** @brief 形を描き切って、書いた線分を返す。 */
	std::vector<fang::DebugLineSegment> BuildLines(const fang::ColliderShape& shape)
	{
		std::vector<fang::DebugLineSegment> segments(fang::GetShapeLineCount(shape));
		const uint32_t                      writtenCount = fang::BuildShapeLines(shape, segments);

		CHECK(writtenCount == segments.size());

		return segments;
	}


	/** @brief 端点がすべて有限か。 */
	bool AreAllPointsFinite(const std::vector<fang::DebugLineSegment>& segments)
	{
		for (const fang::DebugLineSegment& segment : segments)
		{
			const bool isFinite = std::isfinite(segment.from.x) && std::isfinite(segment.from.y) &&
								  std::isfinite(segment.from.z) && std::isfinite(segment.to.x) &&
								  std::isfinite(segment.to.y) && std::isfinite(segment.to.z);
			if (!isFinite)
			{
				return false;
			}
		}

		return true;
	}
} // namespace


TEST_CASE("形ごとの線分の本数が GetShapeLineCount と一致する")
{
	const fang::ColliderShape sphere =
		fang::MakeColliderShape(fang::Sphere{ .center = { 1.0f, 2.0f, 3.0f }, .radius = 2.0f });
	const fang::ColliderShape capsule = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { 0.0f, 0.0f, 0.0f }, .pointB = { 0.0f, 6.0f, 0.0f }, .radius = 1.0f }
	);
	const fang::ColliderShape box =
		fang::MakeColliderShape(fang::OBB{ .center = { 0.0f, 0.0f, 0.0f }, .halfExtents = { 1.0f, 2.0f, 3.0f } });

	CHECK(fang::GetShapeLineCount(sphere) == fang::SPHERE_LINE_COUNT);
	CHECK(fang::GetShapeLineCount(capsule) == fang::CAPSULE_LINE_COUNT);
	CHECK(fang::GetShapeLineCount(box) == fang::BOX_LINE_COUNT);

	CHECK(BuildLines(sphere).size() == 36);
	CHECK(BuildLines(capsule).size() == 52);
	CHECK(BuildLines(box).size() == 12);
}


TEST_CASE("球の線分がすべて表面の上に乗る")
{
	const fang::Sphere        sphere{ .center = { 5.0f, -2.0f, 1.0f }, .radius = 3.0f };
	const fang::ColliderShape shape = fang::MakeColliderShape(sphere);

	const std::vector<fang::DebugLineSegment> segments = BuildLines(shape);
	CHECK(AreAllPointsFinite(segments));

	for (const fang::DebugLineSegment& segment : segments)
	{
		CHECK(fang::Length(segment.from - sphere.center) == doctest::Approx(sphere.radius));
		CHECK(fang::Length(segment.to - sphere.center) == doctest::Approx(sphere.radius));
	}
}


TEST_CASE("OBB の線分が 8 頂点だけを結ぶ")
{
	fang::OBB box;
	box.center      = { 2.0f, 0.0f, 0.0f };
	box.halfExtents = { 1.0f, 2.0f, 3.0f };

	const std::vector<fang::DebugLineSegment> segments = BuildLines(fang::MakeColliderShape(box));

	// どの端点も、中心から各軸へ ±halfExtent 進んだ頂点のどれかに一致する。
	for (const fang::DebugLineSegment& segment : segments)
	{
		const fang::Vector3 points[2] = { segment.from, segment.to };
		for (const fang::Vector3& point : points)
		{
			const fang::Vector3 offset = point - box.center;

			CHECK(std::abs(offset.x) == doctest::Approx(box.halfExtents.x));
			CHECK(std::abs(offset.y) == doctest::Approx(box.halfExtents.y));
			CHECK(std::abs(offset.z) == doctest::Approx(box.halfExtents.z));
		}

		// 辺は 1 軸だけが違う ➡ 2 頂点の差はどれか 1 軸にしか出ない。
		const fang::Vector3 difference       = segment.to - segment.from;
		const int           changedAxisCount = (std::abs(difference.x) > 0.001f ? 1 : 0) +
											   (std::abs(difference.y) > 0.001f ? 1 : 0) +
											   (std::abs(difference.z) > 0.001f ? 1 : 0);
		CHECK(changedAxisCount == 1);
	}
}


TEST_CASE("カプセルの線分が形の外へはみ出さない")
{
	const fang::Capsule       capsule{ .pointA = { 0.0f, 0.0f, 0.0f }, .pointB = { 0.0f, 6.0f, 0.0f }, .radius = 1.5f };
	const fang::ColliderShape shape = fang::MakeColliderShape(capsule);

	const std::vector<fang::DebugLineSegment> segments = BuildLines(shape);
	CHECK(AreAllPointsFinite(segments));

	// どの端点も中心線から半径ぶんの距離に収まる（ワイヤーが形からはみ出していない）。
	const fang::Aabb bounds = fang::ComputeBounds(shape);
	for (const fang::DebugLineSegment& segment : segments)
	{
		const fang::Vector3 points[2] = { segment.from, segment.to };
		for (const fang::Vector3& point : points)
		{
			CHECK(point.x >= bounds.min.x - 0.001f);
			CHECK(point.x <= bounds.max.x + 0.001f);
			CHECK(point.y >= bounds.min.y - 0.001f);
			CHECK(point.y <= bounds.max.y + 0.001f);
			CHECK(point.z >= bounds.min.z - 0.001f);
			CHECK(point.z <= bounds.max.z + 0.001f);
		}
	}
}


TEST_CASE("書き込み先が足りなければそこで打ち切る")
{
	const fang::ColliderShape sphere =
		fang::MakeColliderShape(fang::Sphere{ .center = fang::Vector3{}, .radius = 1.0f });

	std::vector<fang::DebugLineSegment> smallTarget(5);
	CHECK(fang::BuildShapeLines(sphere, smallTarget) == 5);

	CHECK(fang::BuildShapeLines(sphere, std::span<fang::DebugLineSegment>{}) == 0);
}


TEST_CASE("退化した形でも本数が変わらず NaN も出ない")
{
	// 長さ 0 のカプセル。中心線が潰れても、円と側面の本数は同じ。
	const fang::ColliderShape collapsedCapsule = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { 1.0f, 1.0f, 1.0f }, .pointB = { 1.0f, 1.0f, 1.0f }, .radius = 2.0f }
	);
	const std::vector<fang::DebugLineSegment> capsuleSegments = BuildLines(collapsedCapsule);
	CHECK(capsuleSegments.size() == fang::CAPSULE_LINE_COUNT);
	CHECK(AreAllPointsFinite(capsuleSegments));

	// 半径 0 の球。すべての線分が中心に潰れる。
	const fang::ColliderShape pointSphere =
		fang::MakeColliderShape(fang::Sphere{ .center = fang::Vector3{}, .radius = 0.0f });
	CHECK(AreAllPointsFinite(BuildLines(pointSphere)));

	// 大きさ 0 の OBB。
	const fang::ColliderShape flatBox =
		fang::MakeColliderShape(fang::OBB{ .center = fang::Vector3{}, .halfExtents = fang::Vector3{} });
	CHECK(AreAllPointsFinite(BuildLines(flatBox)));
}


TEST_CASE("狼と置き物 42 個のワイヤーがデバッグ描画の上限に収まる")
{
	// 実際の使い方（狼 2 体のカプセル + 置き物 40 個の OBB）で、DebugDraw の 4096 本に収まることを見る。
	const fang::ColliderShape capsule = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { 0.0f, 0.0f, 0.0f }, .pointB = { 0.0f, 100.0f, 0.0f }, .radius = 30.0f }
	);
	const fang::ColliderShape box =
		fang::MakeColliderShape(fang::OBB{ .center = fang::Vector3{}, .halfExtents = { 50.0f, 50.0f, 50.0f } });

	const uint32_t totalLineCount = fang::GetShapeLineCount(capsule) * 2 + fang::GetShapeLineCount(box) * 40;

	CHECK(totalLineCount == 584);
	CHECK(totalLineCount < 4096);
}
