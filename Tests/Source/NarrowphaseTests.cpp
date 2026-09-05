/**
 * @file NarrowphaseTests.cpp
 * @brief 形の組ごとの接触判定のテスト。6 組の点・法線・深さと、種類での振り分けを確かめる。
 */
#include "Collision/Collision.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 成分ごとに近いことを見る。 */
	void CheckVector3(const fang::Vector3& actual, const fang::Vector3& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x));
		CHECK(actual.y == doctest::Approx(expected.y));
		CHECK(actual.z == doctest::Approx(expected.z));
	}


	/** @brief 成分がすべて有限であることを見る。 */
	void CheckFinite(const fang::Vector3& value)
	{
		CHECK(std::isfinite(value.x));
		CHECK(std::isfinite(value.y));
		CHECK(std::isfinite(value.z));
	}


	/** @brief 接触が壊れていないこと。法線の長さが 1、深さが 0 以上、点が有限。 */
	void CheckContactIsSound(const fang::Contact& contact)
	{
		CHECK(fang::Length(contact.normal) == doctest::Approx(1.0f));
		CHECK(contact.depth >= 0.0f);
		CheckFinite(contact.point);
		CHECK(std::isfinite(contact.depth));
	}


	/** @brief 軸平行の OBB。 */
	fang::OBB MakeAxisAlignedBox(const fang::Vector3& center, const fang::Vector3& halfExtents)
	{
		return fang::OBB{ .center = center, .halfExtents = halfExtents };
	}


	/** @brief Y 軸まわりに回した OBB。 */
	fang::OBB MakeYawBox(const fang::Vector3& center, const fang::Vector3& halfExtents, float yawRadians)
	{
		const float cosine = std::cos(yawRadians);
		const float sine   = std::sin(yawRadians);

		fang::OBB result;
		result.center      = center;
		result.axes[0]     = { cosine, 0.0f, -sine };
		result.axes[1]     = { 0.0f, 1.0f, 0.0f };
		result.axes[2]     = { sine, 0.0f, cosine };
		result.halfExtents = halfExtents;

		return result;
	}


	/**
	 * @brief 種類で振り分ける Intersect を、順を入れ替えた 2 回で呼んで整合を見る。
	 * @details 法線が反転し、深さと点が一致すること。異種の組で反転を忘れると押し戻しが吸い込みになる。
	 */
	void CheckSwapIsConsistent(const fang::ColliderShape& a, const fang::ColliderShape& b)
	{
		fang::Contact forward;
		fang::Contact backward;

		CHECK(fang::Intersect(a, b, &forward));
		CHECK(fang::Intersect(b, a, &backward));

		CheckContactIsSound(forward);
		CheckContactIsSound(backward);

		CheckVector3(backward.normal, -forward.normal);
		CHECK(backward.depth == doctest::Approx(forward.depth));
		CheckVector3(backward.point, forward.point);
	}
} // namespace


TEST_CASE("球どうしの接触が中心間の距離で決まる")
{
	const fang::Sphere a{ .center = { 0.0f, 0.0f, 0.0f }, .radius = 2.0f };

	fang::Contact contact;
	CHECK(fang::Intersect(a, fang::Sphere{ .center = { 3.0f, 0.0f, 0.0f }, .radius = 2.0f }, &contact));
	CheckContactIsSound(contact);

	// 半径の和 4 に対して中心間が 3 ➡ 深さ 1、法線は A から B へ。
	CHECK(contact.depth == doctest::Approx(1.0f));
	CheckVector3(contact.normal, fang::Vector3{ 1.0f, 0.0f, 0.0f });
	CheckVector3(contact.point, fang::Vector3{ 1.5f, 0.0f, 0.0f });

	CHECK_FALSE(fang::Intersect(a, fang::Sphere{ .center = { 5.0f, 0.0f, 0.0f }, .radius = 2.0f }, &contact));

	// ちょうど接する ➡ 触れている扱いで深さ 0。
	CHECK(fang::Intersect(a, fang::Sphere{ .center = { 4.0f, 0.0f, 0.0f }, .radius = 2.0f }, &contact));
	CHECK(contact.depth == doctest::Approx(0.0f));
}


TEST_CASE("球とカプセルが中心線の最近点で判定される")
{
	const fang::Capsule capsule{ .pointA = { -5.0f, 0.0f, 0.0f }, .pointB = { 5.0f, 0.0f, 0.0f }, .radius = 1.0f };

	fang::Contact contact;

	// 線分の真上。最近点は原点で、半径の和 2 に対して距離 1.5。
	CHECK(fang::Intersect(fang::Sphere{ .center = { 0.0f, 1.5f, 0.0f }, .radius = 1.0f }, capsule, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ 0.0f, -1.0f, 0.0f });

	// 線分の端の外側。端の半球に当たる。
	CHECK(fang::Intersect(fang::Sphere{ .center = { 6.5f, 0.0f, 0.0f }, .radius = 1.0f }, capsule, &contact));
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ -1.0f, 0.0f, 0.0f });

	CHECK_FALSE(fang::Intersect(fang::Sphere{ .center = { 0.0f, 3.0f, 0.0f }, .radius = 1.0f }, capsule, &contact));
}


TEST_CASE("球と OBB は外からも中からも接触を返す")
{
	const fang::OBB box = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{ 1.0f, 1.0f, 1.0f });

	fang::Contact contact;

	// 面の外。最近点は面の上。
	CHECK(fang::Intersect(fang::Sphere{ .center = { 2.0f, 0.0f, 0.0f }, .radius = 1.5f }, box, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ -1.0f, 0.0f, 0.0f });
	CheckVector3(contact.point, fang::Vector3{ 0.75f, 0.0f, 0.0f });

	// 中心が箱の中。いちばん近い面（+X、0.5 の距離）へ抜く。
	CHECK(fang::Intersect(fang::Sphere{ .center = { 0.5f, 0.0f, 0.0f }, .radius = 0.1f }, box, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.6f));
	CheckVector3(contact.normal, fang::Vector3{ -1.0f, 0.0f, 0.0f });
	CheckVector3(contact.point, fang::Vector3{ 1.0f, 0.0f, 0.0f });

	CHECK_FALSE(fang::Intersect(fang::Sphere{ .center = { 3.0f, 0.0f, 0.0f }, .radius = 1.0f }, box, &contact));
}


TEST_CASE("カプセルどうしが線分の最近点で判定される")
{
	const fang::Capsule alongX{ .pointA = { -5.0f, 0.0f, 0.0f }, .pointB = { 5.0f, 0.0f, 0.0f }, .radius = 1.0f };

	fang::Contact contact;

	// 直交して 1.5 だけ離れた 2 本。最近点は原点と (0, 0, 1.5)。
	const fang::Capsule crossing{ .pointA = { 0.0f, 0.0f, 1.5f }, .pointB = { 0.0f, 5.0f, 1.5f }, .radius = 1.0f };
	CHECK(fang::Intersect(alongX, crossing, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ 0.0f, 0.0f, 1.0f });

	// 平行に並んだ 2 本。分母が 0 になる経路を通る。
	const fang::Capsule parallelClose{ .pointA = { -5.0f, 1.5f, 0.0f },
									   .pointB = { 5.0f, 1.5f, 0.0f },
									   .radius = 1.0f };
	CHECK(fang::Intersect(alongX, parallelClose, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ 0.0f, 1.0f, 0.0f });

	const fang::Capsule parallelFar{ .pointA = { -5.0f, 5.0f, 0.0f }, .pointB = { 5.0f, 5.0f, 0.0f }, .radius = 1.0f };
	CHECK_FALSE(fang::Intersect(alongX, parallelFar, &contact));
}


TEST_CASE("カプセルと OBB が中心線の最近点で判定される")
{
	const fang::OBB box = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{ 1.0f, 1.0f, 1.0f });

	fang::Contact contact;

	// 箱の上を横切る棒。最近点は箱の上面、芯は y = 2。
	const fang::Capsule crossingAbove{ .pointA = { -5.0f, 2.0f, 0.0f },
									   .pointB = { 5.0f, 2.0f, 0.0f },
									   .radius = 1.5f };
	CHECK(fang::Intersect(crossingAbove, box, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ 0.0f, -1.0f, 0.0f });

	// 箱を斜めに貫く棒。中点を 1 回 clamp するだけでは最近点を外すので、反復が効いているかを見る。
	const fang::Capsule diagonal{ .pointA = { -4.0f, -4.0f, 0.0f }, .pointB = { 4.0f, 4.0f, 0.0f }, .radius = 0.25f };
	CHECK(fang::Intersect(diagonal, box, &contact));
	CheckContactIsSound(contact);

	// 箱から離れた棒。
	const fang::Capsule distant{ .pointA = { -5.0f, 5.0f, 0.0f }, .pointB = { 5.0f, 5.0f, 0.0f }, .radius = 1.0f };
	CHECK_FALSE(fang::Intersect(distant, box, &contact));
}


TEST_CASE("OBB どうしが分離軸判定で最も浅い軸を返す")
{
	const fang::OBB origin = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{ 1.0f, 1.0f, 1.0f });

	fang::Contact contact;

	// 軸平行どうし。X 方向の重なり 0.5 がいちばん浅い。
	CHECK(
		fang::Intersect(
			origin,
			MakeAxisAlignedBox(fang::Vector3{ 1.5f, 0.0f, 0.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f }),
			&contact
		)
	);
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.5f));
	CheckVector3(contact.normal, fang::Vector3{ 1.0f, 0.0f, 0.0f });

	CHECK_FALSE(
		fang::Intersect(
			origin,
			MakeAxisAlignedBox(fang::Vector3{ 3.0f, 0.0f, 0.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f }),
			&contact
		)
	);

	// 45 度回した箱。X へ投影した半径が sqrt(2) になるので、重なりは 1 + sqrt(2) - 2.2。
	const float expectedDepth = 1.0f + std::sqrt(2.0f) - 2.2f;
	CHECK(
		fang::Intersect(
			origin,
			MakeYawBox(fang::Vector3{ 2.2f, 0.0f, 0.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f }, fang::PI * 0.25f),
			&contact
		)
	);
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(expectedDepth));
	CheckVector3(contact.normal, fang::Vector3{ 1.0f, 0.0f, 0.0f });
}


TEST_CASE("種類で振り分ける Intersect が 9 通りすべてで整合する")
{
	// 同種の組も別の位置の 2 つで見る。まったく同じ形どうしだと法線の向きが決められず、反転の確認にならない。
	const fang::ColliderShape sphere =
		fang::MakeColliderShape(fang::Sphere{ .center = { 0.0f, 0.0f, 0.0f }, .radius = 1.2f });
	const fang::ColliderShape otherSphere =
		fang::MakeColliderShape(fang::Sphere{ .center = { 1.5f, 0.0f, 0.0f }, .radius = 1.0f });

	const fang::ColliderShape capsule = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { -3.0f, 0.5f, 0.0f }, .pointB = { 3.0f, 0.5f, 0.0f }, .radius = 0.8f }
	);
	const fang::ColliderShape otherCapsule = fang::MakeColliderShape(
		fang::Capsule{ .pointA = { 0.0f, -2.0f, 1.0f }, .pointB = { 0.0f, 2.0f, 1.0f }, .radius = 0.6f }
	);

	const fang::ColliderShape box = fang::MakeColliderShape(
		MakeAxisAlignedBox(fang::Vector3{ 1.5f, 0.0f, 0.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f })
	);
	const fang::ColliderShape otherBox = fang::MakeColliderShape(
		MakeAxisAlignedBox(fang::Vector3{ 0.0f, 1.2f, 0.0f }, fang::Vector3{ 0.8f, 0.8f, 0.8f })
	);

	CheckSwapIsConsistent(sphere, otherSphere);
	CheckSwapIsConsistent(sphere, capsule);
	CheckSwapIsConsistent(sphere, box);
	CheckSwapIsConsistent(capsule, otherCapsule);
	CheckSwapIsConsistent(capsule, box);
	CheckSwapIsConsistent(box, otherBox);

	// 離れていれば、どちらの順でも触れていないと答える。
	const fang::ColliderShape farSphere =
		fang::MakeColliderShape(fang::Sphere{ .center = { 100.0f, 0.0f, 0.0f }, .radius = 1.0f });

	fang::Contact contact;
	CHECK_FALSE(fang::Intersect(farSphere, box, &contact));
	CHECK_FALSE(fang::Intersect(box, farSphere, &contact));
	CHECK_FALSE(fang::Intersect(farSphere, capsule, &contact));
	CHECK_FALSE(fang::Intersect(capsule, farSphere, &contact));
}


TEST_CASE("退化した形でも NaN を返さない")
{
	fang::Contact contact;

	// 半径 0 の球が同じ位置で重なる ➡ 向きが決められない。既定の押し出し方向が入る。
	const fang::Sphere point{ .center = { 1.0f, 1.0f, 1.0f }, .radius = 0.0f };
	CHECK(fang::Intersect(point, point, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.0f));

	// 長さ 0 のカプセル（実質は球）どうし。
	const fang::Capsule collapsed{ .pointA = { 0.0f, 0.0f, 0.0f }, .pointB = { 0.0f, 0.0f, 0.0f }, .radius = 1.0f };
	CHECK(fang::Intersect(collapsed, collapsed, &contact));
	CheckContactIsSound(contact);

	// 大きさ 0 の OBB。軸は残っているので判定はできる。
	const fang::OBB flat = MakeAxisAlignedBox(fang::Vector3{}, fang::Vector3{});
	CHECK(fang::Intersect(flat, flat, &contact));
	CheckContactIsSound(contact);
	CHECK(contact.depth == doctest::Approx(0.0f));

	// 大きさ 0 の OBB と、それを含む球。
	CHECK(fang::Intersect(fang::Sphere{ .center = { 0.0f, 0.0f, 0.0f }, .radius = 1.0f }, flat, &contact));
	CheckContactIsSound(contact);

	// 潰れたカプセルと大きさ 0 の箱。反復の途中で 0 除算が出ないこと。
	CHECK(fang::Intersect(collapsed, flat, &contact));
	CheckContactIsSound(contact);
}
