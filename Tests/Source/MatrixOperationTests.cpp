/**
 * @file MatrixOperationTests.cpp
 * @brief 行列の基本演算のテスト。TransformPoint と TransformDirection の向きの違い、GetRow / GetColumn を確かめる。
 */
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include <doctest.h>


namespace
{
	/** @brief 平行移動の行列。行ベクトル規約なので移動量は最終行に入る。 */
	fang::Matrix4x4 MakeTranslationMatrix(const fang::Vector3& offset)
	{
		fang::Matrix4x4 result;
		result.m[3][0] = offset.x;
		result.m[3][1] = offset.y;
		result.m[3][2] = offset.z;
		return result;
	}


	/** @brief 拡縮の行列。 */
	fang::Matrix4x4 MakeScalingMatrix(const fang::Vector3& scale)
	{
		fang::Matrix4x4 result;
		result.m[0][0] = scale.x;
		result.m[1][1] = scale.y;
		result.m[2][2] = scale.z;
		return result;
	}
} // namespace


TEST_CASE("単位行列で変換しても点と向きは変わらない")
{
	const fang::Matrix4x4 identity;
	const fang::Vector3   value{ 1.0f, 2.0f, 3.0f };

	const fang::Vector3 point = fang::TransformPoint(value, identity);
	CHECK(point.x == doctest::Approx(value.x));
	CHECK(point.y == doctest::Approx(value.y));
	CHECK(point.z == doctest::Approx(value.z));

	const fang::Vector3 direction = fang::TransformDirection(value, identity);
	CHECK(direction.x == doctest::Approx(value.x));
	CHECK(direction.y == doctest::Approx(value.y));
	CHECK(direction.z == doctest::Approx(value.z));
}


TEST_CASE("TransformPoint は平行移動を足すが、TransformDirection は無視する")
{
	// 行ベクトル規約では平行移動が最終行に入り、方向ベクトルにはそこを掛けない。
	// ここが混ざると、狼の法線が平行移動のたびにずれるような不具合になる。
	const fang::Matrix4x4 translation = MakeTranslationMatrix(fang::Vector3{ 10.0f, 20.0f, 30.0f });
	const fang::Vector3   value{ 1.0f, 2.0f, 3.0f };

	const fang::Vector3 point = fang::TransformPoint(value, translation);
	CHECK(point.x == doctest::Approx(11.0f));
	CHECK(point.y == doctest::Approx(22.0f));
	CHECK(point.z == doctest::Approx(33.0f));

	const fang::Vector3 direction = fang::TransformDirection(value, translation);
	CHECK(direction.x == doctest::Approx(value.x));
	CHECK(direction.y == doctest::Approx(value.y));
	CHECK(direction.z == doctest::Approx(value.z));
}


TEST_CASE("TransformPoint と TransformDirection は拡縮には同じようにかかる")
{
	const fang::Matrix4x4 scaling = MakeScalingMatrix(fang::Vector3{ 2.0f, 3.0f, 4.0f });
	const fang::Vector3   value{ 1.0f, 1.0f, 1.0f };

	const fang::Vector3 point = fang::TransformPoint(value, scaling);
	CHECK(point.x == doctest::Approx(2.0f));
	CHECK(point.y == doctest::Approx(3.0f));
	CHECK(point.z == doctest::Approx(4.0f));

	const fang::Vector3 direction = fang::TransformDirection(value, scaling);
	CHECK(direction.x == doctest::Approx(2.0f));
	CHECK(direction.y == doctest::Approx(3.0f));
	CHECK(direction.z == doctest::Approx(4.0f));
}


TEST_CASE("GetRow と GetColumn がそれぞれの成分を取り出す")
{
	fang::Matrix4x4 matrix;
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			matrix.m[row][column] = static_cast<float>(row * 4 + column);
		}
	}

	const fang::Vector4 row = fang::GetRow(matrix, 2);
	CHECK(row.x == doctest::Approx(8.0f));
	CHECK(row.y == doctest::Approx(9.0f));
	CHECK(row.z == doctest::Approx(10.0f));
	CHECK(row.w == doctest::Approx(11.0f));

	const fang::Vector4 column = fang::GetColumn(matrix, 1);
	CHECK(column.x == doctest::Approx(1.0f));
	CHECK(column.y == doctest::Approx(5.0f));
	CHECK(column.z == doctest::Approx(9.0f));
	CHECK(column.w == doctest::Approx(13.0f));
}
