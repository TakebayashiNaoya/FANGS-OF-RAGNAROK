/**
 * @file AabbTests.cpp
 * @brief 境界ボックスのテスト。無効な既定値、Expand の取り込み、行列変換が 8 頂点を包むことを確かめる。
 */
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 円周率。Core/Math にはまだ定数を置いていないので、回転角を作るためにここで持つ。 */
	constexpr float PI = 3.14159265358979323846f;


	/**
	 * @brief 点を行ベクトルとして左から掛ける（p * M）。
	 * @details 掛ける向きを間違えると転置した結果でも辻褄が合ってしまうので、テスト側でも規約どおりに手で書く。
	 */
	fang::Vector3 TransformPoint(const fang::Matrix4x4& matrix, const fang::Vector3& point)
	{
		return fang::Vector3{
			point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
			point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
			point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2],
		};
	}


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


	/** @brief Y 軸まわりの回転行列。左手系・行ベクトル規約なので、角度を増やすと +X が -Z へ回る。 */
	fang::Matrix4x4 MakeRotationYMatrix(float radians)
	{
		const float cosine = std::cos(radians);
		const float sine   = std::sin(radians);

		fang::Matrix4x4 result;
		result.m[0][0] = cosine;
		result.m[0][2] = -sine;
		result.m[2][0] = sine;
		result.m[2][2] = cosine;
		return result;
	}


	/** @brief 8 頂点を 1 個ずつ変換して包み直す、素直だが遅いやり方。TransformAabb の答え合わせに使う。 */
	fang::Aabb TransformCornersDirectly(const fang::Aabb& bounds, const fang::Matrix4x4& matrix)
	{
		const float xValues[2] = { bounds.min.x, bounds.max.x };
		const float yValues[2] = { bounds.min.y, bounds.max.y };
		const float zValues[2] = { bounds.min.z, bounds.max.z };

		fang::Aabb result;
		for (const float x : xValues)
		{
			for (const float y : yValues)
			{
				for (const float z : zValues)
				{
					result.Expand(TransformPoint(matrix, fang::Vector3{ x, y, z }));
				}
			}
		}
		return result;
	}


	/** @brief 2 つの箱の 6 成分が一致するか確かめる。 */
	void CheckAabbsAreEqual(const fang::Aabb& actual, const fang::Aabb& expected)
	{
		CHECK(actual.min.x == doctest::Approx(expected.min.x));
		CHECK(actual.min.y == doctest::Approx(expected.min.y));
		CHECK(actual.min.z == doctest::Approx(expected.min.z));

		CHECK(actual.max.x == doctest::Approx(expected.max.x));
		CHECK(actual.max.y == doctest::Approx(expected.max.y));
		CHECK(actual.max.z == doctest::Approx(expected.max.z));
	}
} // namespace


TEST_CASE("既定の Aabb は無効")
{
	const fang::Aabb bounds;

	CHECK_FALSE(bounds.IsValid());
}


TEST_CASE("点を 1 つ Expand した箱は、その点だけを含む有効な箱になる")
{
	fang::Aabb bounds;
	bounds.Expand(fang::Vector3{ 3.0f, -4.0f, 5.0f });

	CHECK(bounds.IsValid());
	CheckAabbsAreEqual(bounds, fang::Aabb{ .min = { 3.0f, -4.0f, 5.0f }, .max = { 3.0f, -4.0f, 5.0f } });
}


TEST_CASE("Expand を重ねると全部の点を含むまで広がる")
{
	fang::Aabb bounds;
	bounds.Expand(fang::Vector3{ 1.0f, 2.0f, 3.0f });
	bounds.Expand(fang::Vector3{ -5.0f, 2.0f, 10.0f });
	bounds.Expand(fang::Vector3{ 0.0f, -7.0f, 4.0f });

	CheckAabbsAreEqual(bounds, fang::Aabb{ .min = { -5.0f, -7.0f, 3.0f }, .max = { 1.0f, 2.0f, 10.0f } });

	// すでに内側にある点を入れても広がらない。
	bounds.Expand(fang::Vector3{ 0.0f, 0.0f, 5.0f });
	CheckAabbsAreEqual(bounds, fang::Aabb{ .min = { -5.0f, -7.0f, 3.0f }, .max = { 1.0f, 2.0f, 10.0f } });
}


TEST_CASE("単位行列で変換しても箱は変わらない")
{
	const fang::Aabb bounds{ .min = { -1.0f, -2.0f, -3.0f }, .max = { 4.0f, 5.0f, 6.0f } };

	CheckAabbsAreEqual(fang::TransformAabb(bounds, fang::Matrix4x4{}), bounds);
}


TEST_CASE("TransformAabb は平行移動で箱を大きさそのままに動かす")
{
	const fang::Aabb bounds{ .min = { -1.0f, -2.0f, -3.0f }, .max = { 4.0f, 5.0f, 6.0f } };

	const fang::Matrix4x4 translation = MakeTranslationMatrix(fang::Vector3{ 10.0f, 20.0f, 30.0f });
	const fang::Aabb      moved       = fang::TransformAabb(bounds, translation);

	CheckAabbsAreEqual(moved, fang::Aabb{ .min = { 9.0f, 18.0f, 27.0f }, .max = { 14.0f, 25.0f, 36.0f } });
}


TEST_CASE("TransformAabb は 90 度の回転で軸を入れ替える")
{
	// 各軸の長さを変えてあるので、入れ替わりを取り違えると必ず落ちる。
	const fang::Aabb bounds{ .min = { 0.0f, 0.0f, 0.0f }, .max = { 2.0f, 1.0f, 4.0f } };

	// +90 度で (x, y, z) は (z, y, -x) へ移る。
	const fang::Matrix4x4 rotation = MakeRotationYMatrix(PI / 2.0f);
	const fang::Aabb      rotated  = fang::TransformAabb(bounds, rotation);

	CheckAabbsAreEqual(rotated, fang::Aabb{ .min = { 0.0f, 0.0f, -2.0f }, .max = { 4.0f, 1.0f, 0.0f } });
}


TEST_CASE("TransformAabb は斜めの回転でも 8 頂点を過不足なく包む")
{
	const fang::Aabb bounds{ .min = { -1.0f, -2.0f, -3.0f }, .max = { 4.0f, 5.0f, 6.0f } };

	const fang::Matrix4x4 rotation = MakeRotationYMatrix(PI / 4.0f);
	const fang::Aabb      rotated  = fang::TransformAabb(bounds, rotation);

	// 回転で軸に沿わなくなるぶん、包む箱は元より広がる。
	CHECK(rotated.max.x - rotated.min.x > bounds.max.x - bounds.min.x);
	CHECK(rotated.max.z - rotated.min.z > bounds.max.z - bounds.min.z);
	CHECK(rotated.max.y - rotated.min.y == doctest::Approx(bounds.max.y - bounds.min.y));

	// 中心 + 半径ベクトルのやり方は、箱に対しては 8 頂点を回すのと同じ答えになる（緩まない）。
	CheckAabbsAreEqual(rotated, TransformCornersDirectly(bounds, rotation));
}


TEST_CASE("TransformAabb は拡縮・回転・平行移動を合成した行列でも 8 頂点と一致する")
{
	const fang::Aabb bounds{ .min = { -1.0f, -2.0f, -3.0f }, .max = { 4.0f, 5.0f, 6.0f } };

	// 行ベクトル規約なので、効かせたい順に左から並べる。
	const fang::Matrix4x4 scaling     = MakeScalingMatrix(fang::Vector3{ 2.0f, 0.5f, 3.0f });
	const fang::Matrix4x4 rotation    = MakeRotationYMatrix(PI / 6.0f);
	const fang::Matrix4x4 translation = MakeTranslationMatrix(fang::Vector3{ -7.0f, 8.0f, 9.0f });

	const fang::Matrix4x4 world = fang::Multiply(fang::Multiply(scaling, rotation), translation);

	CheckAabbsAreEqual(fang::TransformAabb(bounds, world), TransformCornersDirectly(bounds, world));
}


TEST_CASE("負の拡縮でも箱は反転せず有効なまま")
{
	const fang::Aabb bounds{ .min = { -1.0f, -2.0f, -3.0f }, .max = { 4.0f, 5.0f, 6.0f } };

	// 半径ベクトルを絶対値の行列で回しているので、鏡像でも min <= max が保たれる。
	const fang::Matrix4x4 mirroring = MakeScalingMatrix(fang::Vector3{ -1.0f, 1.0f, 1.0f });
	const fang::Aabb      mirrored  = fang::TransformAabb(bounds, mirroring);

	CHECK(mirrored.IsValid());
	CheckAabbsAreEqual(mirrored, fang::Aabb{ .min = { -4.0f, -2.0f, -3.0f }, .max = { 1.0f, 5.0f, 6.0f } });
}
