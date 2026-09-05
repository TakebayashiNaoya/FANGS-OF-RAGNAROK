/**
 * @file CollisionShapeTests.cpp
 * @brief 当たり判定の形のテスト。包む箱の計算と、描画用の箱からの導出を確かめる。
 */
#include "Collision/Collision.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 成分ごとに近いことを見る。Vector3 に比較演算子が無いので、テスト側で 1 本持つ。 */
	void CheckVector3(const fang::Vector3& actual, const fang::Vector3& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x));
		CHECK(actual.y == doctest::Approx(expected.y));
		CHECK(actual.z == doctest::Approx(expected.z));
	}


	/** @brief 成分がすべて有限であることを見る。退化した形で NaN や無限が出ていないかの確認に使う。 */
	void CheckFinite(const fang::Vector3& value)
	{
		CHECK(std::isfinite(value.x));
		CHECK(std::isfinite(value.y));
		CHECK(std::isfinite(value.z));
	}


	/**
	 * @brief Y 軸まわりに回して平行移動する行列を作る。
	 * @details 行ベクトル規約（p * M）なので、行 0〜2 がそれぞれ回した後の X / Y / Z 軸になる。
	 */
	fang::Matrix4x4 MakeYawTranslationMatrix(float yawRadians, const fang::Vector3& translation)
	{
		const float cosine = std::cos(yawRadians);
		const float sine   = std::sin(yawRadians);

		fang::Matrix4x4 result;
		result.m[0][0] = cosine;
		result.m[0][2] = -sine;
		result.m[2][0] = sine;
		result.m[2][2] = cosine;

		result.m[3][0] = translation.x;
		result.m[3][1] = translation.y;
		result.m[3][2] = translation.z;

		return result;
	}


	/** @brief 各軸に別々の拡大率を掛ける行列。 */
	fang::Matrix4x4 MakeScaleMatrix(const fang::Vector3& scale)
	{
		fang::Matrix4x4 result;
		result.m[0][0] = scale.x;
		result.m[1][1] = scale.y;
		result.m[2][2] = scale.z;

		return result;
	}


	/** @brief 半径ベクトルから箱を作る。 */
	fang::Aabb MakeCenteredAabb(const fang::Vector3& halfExtents)
	{
		return fang::Aabb{
			.min = fang::Vector3{ -halfExtents.x, -halfExtents.y, -halfExtents.z },
			.max = halfExtents,
		};
	}
} // namespace


TEST_CASE("MakeColliderShape が種類を書き分ける")
{
	const fang::ColliderShape sphere =
		fang::MakeColliderShape(fang::Sphere{ .center = { 1.0f, 2.0f, 3.0f }, .radius = 4.0f });
	CHECK(sphere.type == fang::EnShapeType::Sphere);
	CHECK(sphere.sphere.radius == doctest::Approx(4.0f));

	const fang::ColliderShape capsule = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { 0.0f, 0.0f, 0.0f }, .pointB = { 0.0f, 5.0f, 0.0f }, .radius = 1.0f }
	);
	CHECK(capsule.type == fang::EnShapeType::Capsule);
	CheckVector3(capsule.capsule.pointB, fang::Vector3{ 0.0f, 5.0f, 0.0f });

	const fang::ColliderShape box =
		fang::MakeColliderShape(fang::OBB{ .center = { 7.0f, 0.0f, 0.0f }, .halfExtents = { 1.0f, 1.0f, 1.0f } });
	CHECK(box.type == fang::EnShapeType::OBB);
	CheckVector3(box.obb.center, fang::Vector3{ 7.0f, 0.0f, 0.0f });
}


TEST_CASE("球とカプセルを包む箱が半径ぶん膨らむ")
{
	const fang::Aabb sphereBounds =
		fang::ComputeBounds(fang::MakeColliderShape(fang::Sphere{ .center = { 1.0f, 2.0f, 3.0f }, .radius = 2.0f }));
	CheckVector3(sphereBounds.min, fang::Vector3{ -1.0f, 0.0f, 1.0f });
	CheckVector3(sphereBounds.max, fang::Vector3{ 3.0f, 4.0f, 5.0f });

	// 線分の両端を包んでから半径ぶん広げる ➡ 端の半球まで入る。
	const fang::Aabb capsuleBounds = fang::ComputeBounds(
		fang::MakeColliderShape(
			fang::Capsule{ .pointA = { 0.0f, -4.0f, 0.0f }, .pointB = { 0.0f, 4.0f, 0.0f }, .radius = 1.0f }
		)
	);
	CheckVector3(capsuleBounds.min, fang::Vector3{ -1.0f, -5.0f, -1.0f });
	CheckVector3(capsuleBounds.max, fang::Vector3{ 1.0f, 5.0f, 1.0f });
}


TEST_CASE("回した OBB を包む箱が 3 軸の絶対値の和になる")
{
	// 半径 (1, 2, 3) の箱を Y 軸まわりに 90 度回すと、X と Z の半径が入れ替わる。
	fang::OBB box;
	box.center      = { 10.0f, 0.0f, 0.0f };
	box.axes[0]     = { 0.0f, 0.0f, -1.0f };
	box.axes[1]     = { 0.0f, 1.0f, 0.0f };
	box.axes[2]     = { 1.0f, 0.0f, 0.0f };
	box.halfExtents = { 1.0f, 2.0f, 3.0f };

	const fang::Aabb bounds = fang::ComputeBounds(fang::MakeColliderShape(box));
	CheckVector3(bounds.min, fang::Vector3{ 7.0f, -2.0f, -1.0f });
	CheckVector3(bounds.max, fang::Vector3{ 13.0f, 2.0f, 1.0f });
}


TEST_CASE("MakeOBBFromAabb が world の回転を軸へ、拡大率を半径へ移す")
{
	const fang::Aabb localBounds = MakeCenteredAabb(fang::Vector3{ 1.0f, 2.0f, 3.0f });

	// 平行移動だけ ➡ 軸は単位のまま、半径も変わらない。
	const fang::OBB translated =
		fang::MakeOBBFromAabb(localBounds, MakeYawTranslationMatrix(0.0f, fang::Vector3{ 5.0f, 6.0f, 7.0f }));
	CheckVector3(translated.center, fang::Vector3{ 5.0f, 6.0f, 7.0f });
	CheckVector3(translated.axes[0], fang::Vector3{ 1.0f, 0.0f, 0.0f });
	CheckVector3(translated.halfExtents, fang::Vector3{ 1.0f, 2.0f, 3.0f });

	// Y 軸まわりに 90 度 ➡ X 軸が -Z を、Z 軸が +X を向く。半径は軸に付いて回るので数値は変わらない。
	const fang::OBB rotated = fang::MakeOBBFromAabb(
		localBounds,
		MakeYawTranslationMatrix(fang::PI * 0.5f, fang::Vector3{ 10.0f, 0.0f, 0.0f })
	);
	CheckVector3(rotated.center, fang::Vector3{ 10.0f, 0.0f, 0.0f });
	CheckVector3(rotated.axes[0], fang::Vector3{ 0.0f, 0.0f, -1.0f });
	CheckVector3(rotated.axes[2], fang::Vector3{ 1.0f, 0.0f, 0.0f });
	CheckVector3(rotated.halfExtents, fang::Vector3{ 1.0f, 2.0f, 3.0f });

	// 拡大率は軸から抜いて半径へ移す ➡ 軸は長さ 1 のまま。
	const fang::OBB scaled = fang::MakeOBBFromAabb(localBounds, MakeScaleMatrix(fang::Vector3{ 2.0f, 3.0f, 4.0f }));
	CheckVector3(scaled.axes[0], fang::Vector3{ 1.0f, 0.0f, 0.0f });
	CheckVector3(scaled.axes[1], fang::Vector3{ 0.0f, 1.0f, 0.0f });
	CheckVector3(scaled.halfExtents, fang::Vector3{ 2.0f, 6.0f, 12.0f });
}


TEST_CASE("MakeCapsuleFromAabb が最長軸に沿い、元の箱に収まる")
{
	// 半径 (1, 2, 3) ➡ 最長は Z。残り 2 軸の小さいほう（1）が半径、線分の半長は 3 - 1 = 2。
	const fang::Aabb    localBounds = MakeCenteredAabb(fang::Vector3{ 1.0f, 2.0f, 3.0f });
	const fang::Capsule capsule     = fang::MakeCapsuleFromAabb(
		localBounds,
		MakeYawTranslationMatrix(fang::PI * 0.5f, fang::Vector3{ 10.0f, 0.0f, 0.0f })
	);

	CHECK(capsule.radius == doctest::Approx(1.0f));

	// 回した後の Z 軸は +X を向く ➡ 線分は X 方向に伸びる。
	CheckVector3(capsule.pointA, fang::Vector3{ 8.0f, 0.0f, 0.0f });
	CheckVector3(capsule.pointB, fang::Vector3{ 12.0f, 0.0f, 0.0f });

	// カプセルを包む箱が、同じ world で作った OBB を包む箱に収まっていること。
	const fang::Aabb capsuleBounds = fang::ComputeBounds(fang::MakeColliderShape(capsule));
	const fang::Aabb boxBounds     = fang::ComputeBounds(
		fang::MakeColliderShape(
			fang::MakeOBBFromAabb(
				localBounds,
				MakeYawTranslationMatrix(fang::PI * 0.5f, fang::Vector3{ 10.0f, 0.0f, 0.0f })
			)
		)
	);

	CHECK(capsuleBounds.min.x >= boxBounds.min.x - 0.001f);
	CHECK(capsuleBounds.min.y >= boxBounds.min.y - 0.001f);
	CHECK(capsuleBounds.min.z >= boxBounds.min.z - 0.001f);
	CHECK(capsuleBounds.max.x <= boxBounds.max.x + 0.001f);
	CHECK(capsuleBounds.max.y <= boxBounds.max.y + 0.001f);
	CHECK(capsuleBounds.max.z <= boxBounds.max.z + 0.001f);
}


TEST_CASE("立方体から作ったカプセルは球に潰れる")
{
	// 3 軸が同じ長さ ➡ 半径が最長軸の半径と等しく、線分の長さが 0 になる。
	const fang::Capsule capsule =
		fang::MakeCapsuleFromAabb(MakeCenteredAabb(fang::Vector3{ 2.0f, 2.0f, 2.0f }), fang::Matrix4x4{});

	CHECK(capsule.radius == doctest::Approx(2.0f));
	CheckVector3(capsule.pointA, capsule.pointB);
}


TEST_CASE("退化した形でも NaN を返さない")
{
	const fang::Aabb zeroSphere =
		fang::ComputeBounds(fang::MakeColliderShape(fang::Sphere{ .center = { 1.0f, 1.0f, 1.0f }, .radius = 0.0f }));
	CheckFinite(zeroSphere.min);
	CheckFinite(zeroSphere.max);
	CHECK(zeroSphere.IsValid());

	const fang::Aabb zeroCapsule = fang::ComputeBounds(
		fang::MakeColliderShape(
			fang::Capsule{ .pointA = { 2.0f, 2.0f, 2.0f }, .pointB = { 2.0f, 2.0f, 2.0f }, .radius = 0.0f }
		)
	);
	CheckFinite(zeroCapsule.min);
	CheckFinite(zeroCapsule.max);
	CHECK(zeroCapsule.IsValid());

	// 大きさ 0 の箱と、拡大率 0 の行列。どちらも軸の向きを決められないが、既定の単位軸が残る。
	const fang::Aabb zeroBox = fang::ComputeBounds(
		fang::MakeColliderShape(fang::MakeOBBFromAabb(MakeCenteredAabb(fang::Vector3{}), fang::Matrix4x4{}))
	);
	CheckFinite(zeroBox.min);
	CheckFinite(zeroBox.max);

	const fang::OBB collapsed =
		fang::MakeOBBFromAabb(MakeCenteredAabb(fang::Vector3{ 1.0f, 1.0f, 1.0f }), MakeScaleMatrix(fang::Vector3{}));
	CheckFinite(collapsed.axes[0]);
	CheckFinite(collapsed.axes[1]);
	CheckFinite(collapsed.axes[2]);
	CheckVector3(collapsed.halfExtents, fang::Vector3{});
}
