/**
 * @file Matrix4x4Tests.cpp
 * @brief 行列のテスト。積の値と掛ける順、ビュー行列、透視投影の深度範囲を既知の値で確かめる。
 */
#include "Core/Math/MathConstants.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>


namespace
{
	/** @brief 行ベクトルとして変換した点。w で割る前の同次座標。 */
	struct TransformedPoint
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;
	};


	/**
	 * @brief 点を行ベクトルとして左から掛ける（p * M）。
	 * @details 掛ける向きを間違えると転置した結果でも辻褄が合ってしまうので、テスト側でも規約どおりに手で書く。
	 */
	TransformedPoint TransformPoint(const fang::Matrix4x4& matrix, const fang::Vector3& point)
	{
		TransformedPoint result;
		result.x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
		result.y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
		result.z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
		result.w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
		return result;
	}


	/** @brief 16 成分すべてが一致するか確かめる。 */
	void CheckMatricesAreEqual(const fang::Matrix4x4& actual, const fang::Matrix4x4& expected)
	{
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				CHECK_MESSAGE(
					actual.m[row][column] == doctest::Approx(expected.m[row][column]),
					"m[",
					row,
					"][",
					column,
					"] が食い違う"
				);
			}
		}
	}
} // namespace


TEST_CASE("Matrix4x4 の既定値は単位行列")
{
	const fang::Matrix4x4 identity;

	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			const float expected = (row == column) ? 1.0f : 0.0f;
			CHECK(identity.m[row][column] == doctest::Approx(expected));
		}
	}
}


TEST_CASE("単位行列との積は元の行列を変えない")
{
	const fang::Matrix4x4 identity;

	fang::Matrix4x4 source;
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			source.m[row][column] = static_cast<float>(row * 4 + column + 1);
		}
	}

	CheckMatricesAreEqual(fang::Multiply(source, identity), source);
	CheckMatricesAreEqual(fang::Multiply(identity, source), source);
}


TEST_CASE("行列の積が手計算と一致する")
{
	// 1..16 と 17..32 を並べた行列。成分は行と列の内積で、桁が小さいので float でも誤差なく出る。
	fang::Matrix4x4 left;
	fang::Matrix4x4 right;
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			left.m[row][column]  = static_cast<float>(row * 4 + column + 1);
			right.m[row][column] = static_cast<float>(row * 4 + column + 17);
		}
	}

	// clang-format off
	const float expectedValues[4][4] = {
		{  250.0f,  260.0f,  270.0f,  280.0f },
		{  618.0f,  644.0f,  670.0f,  696.0f },
		{  986.0f, 1028.0f, 1070.0f, 1112.0f },
		{ 1354.0f, 1412.0f, 1470.0f, 1528.0f },
	};
	// clang-format on

	fang::Matrix4x4 expected;
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			expected.m[row][column] = expectedValues[row][column];
		}
	}

	CheckMatricesAreEqual(fang::Multiply(left, right), expected);
}


TEST_CASE("Multiply は左の変換を先に効かせる")
{
	// 平行移動は行ベクトル規約なので最終行に入る。
	fang::Matrix4x4 translation;
	translation.m[3][0] = 10.0f;
	translation.m[3][1] = 20.0f;
	translation.m[3][2] = 30.0f;

	fang::Matrix4x4 scaling;
	scaling.m[0][0] = 2.0f;
	scaling.m[1][1] = 3.0f;
	scaling.m[2][2] = 4.0f;

	const fang::Vector3 point{ 1.0f, 1.0f, 1.0f };

	// 移動してから拡大 ➡ 移動量まで拡大される。
	const TransformedPoint movedThenScaled = TransformPoint(fang::Multiply(translation, scaling), point);
	CHECK(movedThenScaled.x == doctest::Approx(22.0f));
	CHECK(movedThenScaled.y == doctest::Approx(63.0f));
	CHECK(movedThenScaled.z == doctest::Approx(124.0f));
	CHECK(movedThenScaled.w == doctest::Approx(1.0f));

	// 拡大してから移動 ➡ 移動量はそのまま足される。
	const TransformedPoint scaledThenMoved = TransformPoint(fang::Multiply(scaling, translation), point);
	CHECK(scaledThenMoved.x == doctest::Approx(12.0f));
	CHECK(scaledThenMoved.y == doctest::Approx(23.0f));
	CHECK(scaledThenMoved.z == doctest::Approx(34.0f));
	CHECK(scaledThenMoved.w == doctest::Approx(1.0f));
}


TEST_CASE("ビュー行列が視点を原点へ、注視点を +Z へ移す")
{
	// 原点から 10cm 手前に立ち、原点を見る。視線が Z 軸に重なるので結果を手計算できる。
	const fang::Vector3 eye{ 0.0f, 0.0f, -10.0f };
	const fang::Vector3 target{ 0.0f, 0.0f, 0.0f };
	const fang::Vector3 up{ 0.0f, 1.0f, 0.0f };

	const fang::Matrix4x4 view = fang::MakeLookAtMatrix(eye, target, up);

	const TransformedPoint transformedEye = TransformPoint(view, eye);
	CHECK(transformedEye.x == doctest::Approx(0.0f));
	CHECK(transformedEye.y == doctest::Approx(0.0f));
	CHECK(transformedEye.z == doctest::Approx(0.0f));
	CHECK(transformedEye.w == doctest::Approx(1.0f));

	const TransformedPoint transformedTarget = TransformPoint(view, target);
	CHECK(transformedTarget.x == doctest::Approx(0.0f));
	CHECK(transformedTarget.y == doctest::Approx(0.0f));
	CHECK(transformedTarget.z == doctest::Approx(10.0f));
	CHECK(transformedTarget.w == doctest::Approx(1.0f));

	// 視点と同じ奥行きで右上へずらした点は、ビュー座標でもそのまま右上に残る。
	const TransformedPoint transformedOffset = TransformPoint(view, fang::Vector3{ 3.0f, 4.0f, -10.0f });
	CHECK(transformedOffset.x == doctest::Approx(3.0f));
	CHECK(transformedOffset.y == doctest::Approx(4.0f));
	CHECK(transformedOffset.z == doctest::Approx(0.0f));
}


TEST_CASE("ビュー行列が左手系の軸を作る")
{
	// 原点から +X を向く。左手系なら「前 = +X、上 = +Y」に対して右は -Z になる。
	const fang::Vector3 eye{ 0.0f, 0.0f, 0.0f };
	const fang::Vector3 target{ 100.0f, 0.0f, 0.0f };
	const fang::Vector3 up{ 0.0f, 1.0f, 0.0f };

	const fang::Matrix4x4 view = fang::MakeLookAtMatrix(eye, target, up);

	// 注視点はビュー座標の正面（+Z）。
	const TransformedPoint front = TransformPoint(view, target);
	CHECK(front.x == doctest::Approx(0.0f));
	CHECK(front.y == doctest::Approx(0.0f));
	CHECK(front.z == doctest::Approx(100.0f));

	// ワールドの +Z はカメラから見て左（-X）。右手系ならここが +100 になる。
	const TransformedPoint left = TransformPoint(view, fang::Vector3{ 0.0f, 0.0f, 100.0f });
	CHECK(left.x == doctest::Approx(-100.0f));
	CHECK(left.y == doctest::Approx(0.0f));
	CHECK(left.z == doctest::Approx(0.0f));

	// ワールドの +Y はビュー座標でも上（+Y）。
	const TransformedPoint above = TransformPoint(view, fang::Vector3{ 0.0f, 50.0f, 0.0f });
	CHECK(above.x == doctest::Approx(0.0f));
	CHECK(above.y == doctest::Approx(50.0f));
	CHECK(above.z == doctest::Approx(0.0f));
}


TEST_CASE("透視投影の深度が近平面で 0、遠平面で 1 になる")
{
	constexpr float nearZ = 10.0f;
	constexpr float farZ  = 1000.0f;

	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(fang::PI / 2.0f, 16.0f / 9.0f, nearZ, farZ);

	// D3D の深度範囲は [0, 1]。OpenGL 流の [-1, 1] になっていれば近平面が -1 になって落ちる。
	const TransformedPoint onNearPlane = TransformPoint(projection, fang::Vector3{ 0.0f, 0.0f, nearZ });
	CHECK(onNearPlane.w == doctest::Approx(nearZ));
	CHECK(onNearPlane.z / onNearPlane.w == doctest::Approx(0.0f));

	const TransformedPoint onFarPlane = TransformPoint(projection, fang::Vector3{ 0.0f, 0.0f, farZ });
	CHECK(onFarPlane.w == doctest::Approx(farZ));
	CHECK(onFarPlane.z / onFarPlane.w == doctest::Approx(1.0f));

	// w にビュー座標の z がそのまま入る ➡ 左手系。右手系なら w が -z になって符号が逆になる。
	const TransformedPoint between = TransformPoint(projection, fang::Vector3{ 0.0f, 0.0f, 500.0f });
	CHECK(between.w == doctest::Approx(500.0f));
	CHECK(between.z / between.w > 0.0f);
	CHECK(between.z / between.w < 1.0f);
}


TEST_CASE("透視投影が画角と縦横比どおりに視錐台の端を ±1 へ移す")
{
	constexpr float aspect   = 16.0f / 9.0f;
	constexpr float distance = 100.0f;

	// 垂直画角 90 度 ➡ 距離 100cm での縦の半分の高さは 100cm、横はその aspect 倍。
	const fang::Matrix4x4 projection = fang::MakePerspectiveMatrix(fang::PI / 2.0f, aspect, 10.0f, 1000.0f);

	const TransformedPoint top = TransformPoint(projection, fang::Vector3{ 0.0f, distance, distance });
	CHECK(top.y / top.w == doctest::Approx(1.0f));

	const TransformedPoint right = TransformPoint(projection, fang::Vector3{ distance * aspect, 0.0f, distance });
	CHECK(right.x / right.w == doctest::Approx(1.0f));

	const TransformedPoint bottomLeft =
		TransformPoint(projection, fang::Vector3{ -distance * aspect, -distance, distance });
	CHECK(bottomLeft.x / bottomLeft.w == doctest::Approx(-1.0f));
	CHECK(bottomLeft.y / bottomLeft.w == doctest::Approx(-1.0f));
}
