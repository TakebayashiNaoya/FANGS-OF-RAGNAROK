/**
 * @file FrustumTests.cpp
 * @brief 視錐台のテスト。平面の抽出が正規化された内向き法線を作ること、内・外・跨ぎの判定を確かめる。
 */
#include "Core/Math/Aabb.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 円周率。Core/Math にはまだ定数を置いていないので、画角を作るためにここで持つ。 */
	constexpr float PI = 3.14159265358979323846f;

	/** @brief 判定の対象にする箱の一辺の半分。視錐台の大きさに比べて十分小さくしてある。 */
	constexpr float PROBE_RADIUS = 0.5f;


	/** @brief 2 点から箱を作る。 */
	fang::Aabb MakeAabb(const fang::Vector3& minimum, const fang::Vector3& maximum)
	{
		return fang::Aabb{ .min = minimum, .max = maximum };
	}


	/** @brief 点を中心にした小さな立方体。点そのものが内か外かを見るために使う。 */
	fang::Aabb MakeProbeAabb(const fang::Vector3& center)
	{
		return MakeAabb(
			fang::Vector3{ center.x - PROBE_RADIUS, center.y - PROBE_RADIUS, center.z - PROBE_RADIUS },
			fang::Vector3{ center.x + PROBE_RADIUS, center.y + PROBE_RADIUS, center.z + PROBE_RADIUS }
		);
	}
} // namespace


TEST_CASE("抽出した 6 平面の法線が正規化されている")
{
	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(PI / 2.0f, 1.0f, 1.0f, 100.0f);

	fang::Frustum frustum;
	frustum.ExtractFromViewProjection(projection);

	for (const fang::Vector4& plane : frustum.planes)
	{
		const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
		CHECK(length == doctest::Approx(1.0f));
	}
}


TEST_CASE("近平面と遠平面が射影行列の値どおりに出る")
{
	constexpr float nearZ = 1.0f;
	constexpr float farZ  = 100.0f;

	// カメラは原点で +Z を向く。ビュー行列を掛けないので、平面の係数を手計算と直に比べられる。
	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(PI / 2.0f, 1.0f, nearZ, farZ);

	fang::Frustum frustum;
	frustum.ExtractFromViewProjection(projection);

	// 近平面は z - near >= 0、遠平面は far - z >= 0。どちらも法線は視錐台の内側を向く。
	const fang::Vector4& nearPlane = frustum.planes[4];
	CHECK(nearPlane.x == doctest::Approx(0.0f));
	CHECK(nearPlane.y == doctest::Approx(0.0f));
	CHECK(nearPlane.z == doctest::Approx(1.0f));
	CHECK(nearPlane.w == doctest::Approx(-nearZ));

	const fang::Vector4& farPlane = frustum.planes[5];
	CHECK(farPlane.x == doctest::Approx(0.0f));
	CHECK(farPlane.y == doctest::Approx(0.0f));
	CHECK(farPlane.z == doctest::Approx(-1.0f));
	CHECK(farPlane.w == doctest::Approx(farZ));
}


TEST_CASE("原点のカメラで、視錐台の内と外を見分ける")
{
	// 垂直画角 90 度・縦横比 1 ➡ 視錐台の中は |x| <= z、|y| <= z、1 <= z <= 100。
	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(PI / 2.0f, 1.0f, 1.0f, 100.0f);

	fang::Frustum frustum;
	frustum.ExtractFromViewProjection(projection);

	CHECK(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, 50.0f })));
	CHECK(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 40.0f, -40.0f, 50.0f })));

	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, 0.2f })));
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, 150.0f })));
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 60.0f, 0.0f, 50.0f })));
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, -60.0f, 50.0f })));

	// カメラの真後ろ。近平面だけでなく遠平面の裏でもあるので、符号を間違えると通ってしまう。
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, -50.0f })));
}


TEST_CASE("近平面を跨ぐ箱と、視錐台を丸ごと含む箱は交差と判定される")
{
	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(PI / 2.0f, 1.0f, 1.0f, 100.0f);

	fang::Frustum frustum;
	frustum.ExtractFromViewProjection(projection);

	// 近平面（z = 1）の手前から奥まで伸びる箱。一部でも入っていれば描くので true。
	const fang::Aabb straddlingNearPlane =
		MakeAabb(fang::Vector3{ -0.2f, -0.2f, 0.5f }, fang::Vector3{ 0.2f, 0.2f, 2.0f });
	CHECK(frustum.Intersects(straddlingNearPlane));

	// 横の平面を跨ぐ箱。中心は視錐台の外にあるが、内側の角が入っている。
	const fang::Aabb straddlingSidePlane =
		MakeAabb(fang::Vector3{ 40.0f, -1.0f, 49.0f }, fang::Vector3{ 60.0f, 1.0f, 51.0f });
	CHECK(frustum.Intersects(straddlingSidePlane));

	// 視錐台をすべて飲み込む箱。どの平面から見ても外側の角が表にあるので true。
	const fang::Aabb enclosing =
		MakeAabb(fang::Vector3{ -1000.0f, -1000.0f, -1000.0f }, fang::Vector3{ 1000.0f, 1000.0f, 1000.0f });
	CHECK(frustum.Intersects(enclosing));
}


TEST_CASE("ビュー行列を掛けた視錐台がカメラの位置と向きに追従する")
{
	// 原点の 10cm 手前から原点を見る ➡ 視錐台は z = -9 から z = 90 まで伸びる。
	const fang::Matrix4x4 view = fang::MakeLookAtMatrix(
		fang::Vector3{ 0.0f, 0.0f, -10.0f },
		fang::Vector3{ 0.0f, 0.0f, 0.0f },
		fang::Vector3{ 0.0f, 1.0f, 0.0f }
	);
	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(PI / 2.0f, 1.0f, 1.0f, 100.0f);

	fang::Frustum frustum;
	frustum.ExtractFromViewProjection(fang::Multiply(view, projection));

	CHECK(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, 0.0f })));
	CHECK(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, 80.0f })));

	// カメラの後ろ・遠平面の奥・視野の横。ビュー行列を掛け忘れると最初の 1 つが通ってしまう。
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, -50.0f })));
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 0.0f, 0.0f, 120.0f })));
	CHECK_FALSE(frustum.Intersects(MakeProbeAabb(fang::Vector3{ 100.0f, 0.0f, 0.0f })));

	// 近平面（z = -9）を跨ぐ箱。
	const fang::Aabb straddlingNearPlane =
		MakeAabb(fang::Vector3{ -0.2f, -0.2f, -9.5f }, fang::Vector3{ 0.2f, 0.2f, -8.0f });
	CHECK(frustum.Intersects(straddlingNearPlane));
}
