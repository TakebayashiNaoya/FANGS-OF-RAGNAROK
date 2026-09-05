/**
 * @file BroadphaseTests.cpp
 * @brief 1 軸スイープのテスト。候補の組が総当たりと一致すること、上限で落ちないことを確かめる。
 */
#include "Collision/Broadphase.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include <doctest.h>
#include <algorithm>
#include <vector>


namespace
{
	/** @brief 決定的な擬似乱数。テストの結果が実行ごとに変わらないよう、標準の乱数を使わない。 */
	class TestRandom
	{
	public:
		/** @brief 0.0 以上 1.0 未満。 */
		[[nodiscard]] float NextUnitFloat()
		{
			m_state = m_state * 1664525u + 1013904223u;

			return static_cast<float>(m_state >> 8) / 16777216.0f;
		}

		/** @brief minimum 以上 maximum 未満。 */
		[[nodiscard]] float NextFloat(float minimum, float maximum)
		{
			return minimum + (maximum - minimum) * NextUnitFloat();
		}


	private:
		uint32_t m_state = 12345u;
	};


	/** @brief 中心と半径から箱を作る。 */
	fang::Aabb MakeAabb(const fang::Vector3& center, const fang::Vector3& halfExtents)
	{
		return fang::Aabb{
			.min = center - halfExtents,
			.max = center + halfExtents,
		};
	}


	/** @brief 3 軸すべてで重なっているか。総当たりの答え合わせに使う。 */
	bool OverlapsOnAllAxes(const fang::Aabb& left, const fang::Aabb& right)
	{
		return left.min.x <= right.max.x && right.min.x <= left.max.x && left.min.y <= right.max.y &&
			   right.min.y <= left.max.y && left.min.z <= right.max.z && right.min.z <= left.max.z;
	}


	/** @brief 組を番号の順に並べる。集合として比べるため。 */
	bool IsPairLess(const fang::ColliderPair& left, const fang::ColliderPair& right)
	{
		return (left.indexA != right.indexA) ? (left.indexA < right.indexA) : (left.indexB < right.indexB);
	}


	/** @brief 総当たりで重なる組を全部集める。 */
	std::vector<fang::ColliderPair> CollectPairsByBruteForce(const std::vector<fang::Aabb>& bounds)
	{
		std::vector<fang::ColliderPair> pairs;
		for (uint32_t indexA = 0; indexA < bounds.size(); ++indexA)
		{
			for (uint32_t indexB = indexA + 1; indexB < bounds.size(); ++indexB)
			{
				if (OverlapsOnAllAxes(bounds[indexA], bounds[indexB]))
				{
					pairs.push_back(fang::ColliderPair{ .indexA = indexA, .indexB = indexB });
				}
			}
		}

		return pairs;
	}


	/** @brief 決まった種から箱をばらまく。半径をばらつかせて、大きさの違う置き物と雑魚が混じる状況にする。 */
	std::vector<fang::Aabb> MakeScatteredBounds(size_t count)
	{
		TestRandom              random;
		std::vector<fang::Aabb> bounds;
		bounds.reserve(count);

		for (size_t index = 0; index < count; ++index)
		{
			const fang::Vector3 center{ random.NextFloat(-100.0f, 100.0f),
										random.NextFloat(-20.0f, 20.0f),
										random.NextFloat(-100.0f, 100.0f) };
			const float         halfExtent = random.NextFloat(1.0f, 12.0f);

			bounds.push_back(MakeAabb(center, fang::Vector3{ halfExtent, halfExtent, halfExtent }));
		}

		return bounds;
	}
} // namespace


TEST_CASE("1 軸スイープの候補が総当たりと一致する")
{
	const std::vector<fang::Aabb> bounds = MakeScatteredBounds(200);

	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 256));

	broadphase.Build(bounds);
	CHECK(broadphase.GetColliderCount() == 200);

	std::vector<fang::ColliderPair> actualPairs(8192);
	const uint32_t                  pairCount = broadphase.CollectPairs(actualPairs);
	actualPairs.resize(pairCount);

	std::vector<fang::ColliderPair> expectedPairs = CollectPairsByBruteForce(bounds);

	// 重なる組が 1 つも無いと、漏れがあっても気付けない。
	CHECK(expectedPairs.size() > 0);

	std::sort(actualPairs.begin(), actualPairs.end(), IsPairLess);
	std::sort(expectedPairs.begin(), expectedPairs.end(), IsPairLess);

	CHECK(actualPairs.size() == expectedPairs.size());
	if (actualPairs.size() == expectedPairs.size())
	{
		bool isSame = true;
		for (size_t index = 0; index < actualPairs.size(); ++index)
		{
			if (actualPairs[index].indexA != expectedPairs[index].indexA ||
				actualPairs[index].indexB != expectedPairs[index].indexB)
			{
				isSame = false;
				break;
			}
		}

		CHECK(isSame);
	}

	broadphase.Shutdown();
}


TEST_CASE("同じ箱で 2 回組み立てても同じ組を返す")
{
	// 2 回目は前のフレームの並びから始まる（整列済みの列への挿入ソート）。結果が変わらないことを見る。
	const std::vector<fang::Aabb> bounds = MakeScatteredBounds(64);

	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 64));

	std::vector<fang::ColliderPair> firstPairs(4096);
	broadphase.Build(bounds);
	const uint32_t firstCount = broadphase.CollectPairs(firstPairs);

	std::vector<fang::ColliderPair> secondPairs(4096);
	broadphase.Build(bounds);
	const uint32_t secondCount = broadphase.CollectPairs(secondPairs);

	CHECK(firstCount == secondCount);
	for (uint32_t index = 0; index < firstCount && index < secondCount; ++index)
	{
		CHECK(firstPairs[index].indexA == secondPairs[index].indexA);
		CHECK(firstPairs[index].indexB == secondPairs[index].indexB);
	}

	broadphase.Shutdown();
}


TEST_CASE("並びが入れ替わっても候補が漏れない")
{
	// 前のフレームの並びを使い回すので、動いた箱が並びを追い越す場面を作る。
	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 8));

	const fang::Vector3 halfExtents{ 1.0f, 1.0f, 1.0f };

	std::vector<fang::Aabb> bounds;
	bounds.push_back(MakeAabb(fang::Vector3{ 0.0f, 0.0f, 0.0f }, halfExtents));
	bounds.push_back(MakeAabb(fang::Vector3{ 10.0f, 0.0f, 0.0f }, halfExtents));
	bounds.push_back(MakeAabb(fang::Vector3{ 20.0f, 0.0f, 0.0f }, halfExtents));

	std::vector<fang::ColliderPair> pairs(16);

	broadphase.Build(bounds);
	CHECK(broadphase.CollectPairs(pairs) == 0);

	// 3 番目を先頭より手前へ動かす ➡ 並びが逆転し、0 番と重なる。
	bounds[2] = MakeAabb(fang::Vector3{ -1.5f, 0.0f, 0.0f }, halfExtents);
	broadphase.Build(bounds);

	CHECK(broadphase.CollectPairs(pairs) == 1);
	CHECK(pairs[0].indexA == 0);
	CHECK(pairs[0].indexB == 2);

	broadphase.Shutdown();
}


TEST_CASE("箱が 0 個でも上限を超えても落ちない")
{
	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 4));

	std::vector<fang::ColliderPair> pairs(16);

	// 0 個。
	broadphase.Build(std::span<const fang::Aabb>{});
	CHECK(broadphase.GetColliderCount() == 0);
	CHECK(broadphase.CollectPairs(pairs) == 0);

	// 上限 4 に対して 10 個。超えたぶんは捨てる。
	const fang::Vector3     halfExtents{ 5.0f, 5.0f, 5.0f };
	std::vector<fang::Aabb> manyBounds;
	for (int index = 0; index < 10; ++index)
	{
		manyBounds.push_back(MakeAabb(fang::Vector3{ static_cast<float>(index), 0.0f, 0.0f }, halfExtents));
	}

	broadphase.Build(manyBounds);
	CHECK(broadphase.GetColliderCount() == 4);

	// 4 個が全部重なるので 6 組。書き込み先が 2 組しか無ければ 2 組で打ち切る。
	CHECK(broadphase.CollectPairs(pairs) == 6);

	std::vector<fang::ColliderPair> smallPairs(2);
	CHECK(broadphase.CollectPairs(smallPairs) == 2);

	broadphase.Shutdown();

	// 二重に呼んでも安全。
	broadphase.Shutdown();
}


TEST_CASE("上限 0 の Broadphase は初期化に失敗する")
{
	fang::SweepAndPruneBroadphase broadphase;
	CHECK_FALSE(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 0));
}


TEST_CASE("領域クエリが総当たりと一致する")
{
	const std::vector<fang::Aabb> bounds = MakeScatteredBounds(200);

	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 256));
	broadphase.Build(bounds);

	const fang::Aabb queryBounds = MakeAabb(fang::Vector3{ 0.0f, 0.0f, 0.0f }, fang::Vector3{ 30.0f, 30.0f, 30.0f });

	std::vector<uint32_t> expectedIndices;
	for (uint32_t index = 0; index < bounds.size(); ++index)
	{
		if (OverlapsOnAllAxes(bounds[index], queryBounds))
		{
			expectedIndices.push_back(index);
		}
	}

	// 重なりが 1 つも無いと、漏れがあっても気付けない。
	CHECK(expectedIndices.size() > 0);

	std::vector<uint32_t> actualIndices(256);
	const uint32_t        actualCount = broadphase.QueryAabb(queryBounds, actualIndices);
	actualIndices.resize(actualCount);

	std::sort(actualIndices.begin(), actualIndices.end());
	std::sort(expectedIndices.begin(), expectedIndices.end());
	CHECK(actualIndices == expectedIndices);

	broadphase.Shutdown();
}


TEST_CASE("領域クエリは書き込み先が足りないぶんを打ち切る")
{
	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 8));

	const fang::Vector3     halfExtents{ 1.0f, 1.0f, 1.0f };
	std::vector<fang::Aabb> bounds;
	for (int index = 0; index < 4; ++index)
	{
		bounds.push_back(MakeAabb(fang::Vector3{ static_cast<float>(index), 0.0f, 0.0f }, halfExtents));
	}
	broadphase.Build(bounds);

	const fang::Aabb queryBounds = MakeAabb(fang::Vector3{ 1.5f, 0.0f, 0.0f }, fang::Vector3{ 10.0f, 10.0f, 10.0f });

	std::vector<uint32_t> fullIndices(8);
	CHECK(broadphase.QueryAabb(queryBounds, fullIndices) == 4);

	std::vector<uint32_t> smallIndices(2);
	CHECK(broadphase.QueryAabb(queryBounds, smallIndices) == 2);

	broadphase.Shutdown();
}


TEST_CASE("重なりの無い領域クエリは 0 件を返す")
{
	fang::SweepAndPruneBroadphase broadphase;
	CHECK(broadphase.Initialize(fang::HeapAllocator::GetInstance(), 8));

	const std::vector<fang::Aabb> bounds{
		MakeAabb(fang::Vector3{ 0.0f, 0.0f, 0.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f })
	};
	broadphase.Build(bounds);

	std::vector<uint32_t> indices(8);
	CHECK(
		broadphase
			.QueryAabb(MakeAabb(fang::Vector3{ 100.0f, 0.0f, 0.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f }), indices) == 0
	);

	broadphase.Shutdown();
}
