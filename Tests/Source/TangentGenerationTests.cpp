/**
 * @file TangentGenerationTests.cpp
 * @brief 接線の生成のテスト。既定の向き、UV を裏返したときの w、UV が退化したときの逃げ場を確かめる。
 */
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include "Renderer/TangentGeneration.h"
#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <vector>


namespace
{
	/** @brief XY 平面の三角形 1 枚。法線は +Z で、UV の u が +X、v が +Y に沿う。 */
	const std::vector<fang::Vector3> TRIANGLE_POSITIONS = {
		{ 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
	};

	const std::vector<fang::Vector3> TRIANGLE_NORMALS = {
		{ 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 1.0f },
	};

	const std::vector<uint16_t> TRIANGLE_INDICES = { 0, 1, 2 };
} // namespace


TEST_CASE("UV が素直に並んだ三角形では、接線が u の向きになり w が +1 になる")
{
	const std::vector<fang::Vector2> texCoords = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };

	std::vector<fang::Vector4> tangents(TRIANGLE_POSITIONS.size());
	fang::GenerateTangents(TRIANGLE_POSITIONS, TRIANGLE_NORMALS, texCoords, TRIANGLE_INDICES, tangents);

	for (const fang::Vector4& tangent : tangents)
	{
		CHECK(tangent.x == doctest::Approx(1.0f));
		CHECK(tangent.y == doctest::Approx(0.0f));
		CHECK(tangent.z == doctest::Approx(0.0f));
		CHECK(tangent.w == doctest::Approx(1.0f));
	}
}


TEST_CASE("UV を裏返した面では接線も裏返り、w が -1 になる")
{
	// u だけを反転した UV。ステージの箱は面の半分がこの並びになるので、ここを取り違えると
	// 同じ箱の面の半分だけが光を裏返して受ける。
	const std::vector<fang::Vector2> texCoords = { { 0.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f } };

	std::vector<fang::Vector4> tangents(TRIANGLE_POSITIONS.size());
	fang::GenerateTangents(TRIANGLE_POSITIONS, TRIANGLE_NORMALS, texCoords, TRIANGLE_INDICES, tangents);

	for (const fang::Vector4& tangent : tangents)
	{
		CHECK(tangent.x == doctest::Approx(-1.0f));
		CHECK(tangent.w == doctest::Approx(-1.0f));
	}
}


TEST_CASE("UV が退化していても、法線と直交する長さ 1 の接線が出る")
{
	SUBCASE("3 頂点の UV が同じ点に潰れている")
	{
		const std::vector<fang::Vector2> texCoords = { { 0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.5f } };

		std::vector<fang::Vector4> tangents(TRIANGLE_POSITIONS.size());
		fang::GenerateTangents(TRIANGLE_POSITIONS, TRIANGLE_NORMALS, texCoords, TRIANGLE_INDICES, tangents);

		for (size_t index = 0; index < tangents.size(); ++index)
		{
			const fang::Vector4& tangent = tangents[index];
			const fang::Vector3  axis{ tangent.x, tangent.y, tangent.z };

			CHECK(fang::Length(axis) == doctest::Approx(1.0f));
			CHECK(fang::Dot(axis, TRIANGLE_NORMALS[index]) == doctest::Approx(0.0f));
			CHECK(std::fabs(tangent.w) == doctest::Approx(1.0f));
		}
	}

	SUBCASE("UV を渡していない")
	{
		std::vector<fang::Vector4> tangents(TRIANGLE_POSITIONS.size());
		fang::GenerateTangents(TRIANGLE_POSITIONS, TRIANGLE_NORMALS, {}, TRIANGLE_INDICES, tangents);

		for (const fang::Vector4& tangent : tangents)
		{
			const fang::Vector3 axis{ tangent.x, tangent.y, tangent.z };

			CHECK(fang::Length(axis) == doctest::Approx(1.0f));
			CHECK(fang::Dot(axis, fang::Vector3{ 0.0f, 0.0f, 1.0f }) == doctest::Approx(0.0f));
		}
	}
}
