/**
 * @file BroadphaseComparisonTests.cpp
 * @brief 3 実装(SweepAndPrune / UniformGrid / DynamicAabbTree)を同じ箱の列で比べる。
 * @details 登録 64/256/1024/4096 × 混在/均一 の 2 分布 × 3 実装 = 24 通りで、候補の組と領域クエリが
 *          総当たりとも互いとも一致すること、Build + CollectPairs の所要時間、確保量を測る。
 *          既定の選び方は 設計.md に書いた規則(登録 256 の 2 分布の合計時間が最短。差 10% 未満なら
 *          確保量の小さいほう)で、ここで出した数字をもとに機械的に決める。
 */
#include "Collision/Broadphase.h"
#include "Collision/CollisionMath.h"
#include "Collision/CollisionWorld.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Core/Platform/Budget.h"
#include <doctest.h>
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <vector>


namespace
{
	/** @brief 計測が回す登録数の 4 段。64 = 今の実物、256 = 今の登録上限。 */
	constexpr uint32_t REGISTERED_COUNTS[4] = { 64, 256, 1024, 4096 };

	/** @brief 計測が回す 3 実装。 */
	constexpr fang::EnBroadphaseType ALL_BROADPHASE_TYPES[fang::BROADPHASE_TYPE_COUNT] = {
		fang::EnBroadphaseType::SweepAndPrune,
		fang::EnBroadphaseType::UniformGrid,
		fang::EnBroadphaseType::DynamicAabbTree,
	};

	/** @brief 候補の組の書き込み先の席数。打ち切りが起きていないことを見るのに使う。 */
	constexpr uint32_t PAIR_BUFFER_CAPACITY = 1u << 20;

	/** @brief 箱の半径(半径 20〜25cm 相当)。均一分布と、混在分布の雑魚ぶんに使う。 */
	constexpr float SMALL_HALF_EXTENT_MIN = 20.0f;
	constexpr float SMALL_HALF_EXTENT_MAX = 25.0f;

	/** @brief 混在分布の置き物ぶんの半径(雑魚の 15〜45 倍)。 */
	constexpr float LARGE_HALF_EXTENT_MIN = 300.0f;
	constexpr float LARGE_HALF_EXTENT_MAX = 900.0f;

	/** @brief 混在分布での置き物の数。実物のステージに合わせて count に比例させない。 */
	constexpr uint32_t MIXED_LARGE_OBJECT_COUNT = 40;


	/** @brief 分布の種類。 */
	enum class EnDistribution : uint8_t
	{
		Uniform, /**< 雑魚だけが密集した状態。 */
		Mixed,   /**< 大きさが数十倍違う置き物 + 雑魚(実際のステージに近い)。 */
	};

	[[nodiscard]] const char* NameOf(EnDistribution distribution)
	{
		return (distribution == EnDistribution::Uniform) ? "均一" : "混在";
	}


	/** @brief 決定的な擬似乱数。計測結果が実行ごとに変わらないよう、標準の乱数を使わない。 */
	class TestRandom
	{
	public:
		[[nodiscard]] float NextFloat(float minimum, float maximum)
		{
			m_state = m_state * 1664525u + 1013904223u;

			return minimum + (maximum - minimum) * (static_cast<float>(m_state >> 8) / 16777216.0f);
		}


	private:
		uint32_t m_state = 20260906u;
	};


	/** @brief 中心と半径から箱を作る。 */
	fang::Aabb MakeAabb(const fang::Vector3& center, const fang::Vector3& halfExtents)
	{
		return fang::Aabb{ .min = center - halfExtents, .max = center + halfExtents };
	}


	/**
	 * @brief 4 段のあいだ密度を一定に保つ配置範囲。一辺 150cm × cbrt(count) の立方体。
	 * @details 範囲を固定したまま数を増やすと、組の数が段ごとに変わりすぎて「Broadphase の速さ」ではなく
	 *          「組の書き出しの速さ」を測ることになる。
	 */
	[[nodiscard]] float ComputePlacementHalfRange(uint32_t count)
	{
		return 75.0f * std::cbrt(static_cast<float>(count));
	}


	/** @brief 計測用の箱の列。均一は全部が雑魚サイズ、混在は 40 個だけ置き物サイズにする。 */
	[[nodiscard]] std::vector<fang::Aabb> MakeBoundsForMeasurement(uint32_t count, EnDistribution distribution)
	{
		TestRandom     random;
		const float    halfRange = ComputePlacementHalfRange(count);
		const uint32_t largeCount =
			(distribution == EnDistribution::Mixed) ? std::min(MIXED_LARGE_OBJECT_COUNT, count) : 0;
		std::vector<fang::Aabb> bounds;
		bounds.reserve(count);

		for (uint32_t index = 0; index < count; ++index)
		{
			const fang::Vector3 center{ random.NextFloat(-halfRange, halfRange),
										random.NextFloat(-halfRange, halfRange),
										random.NextFloat(-halfRange, halfRange) };

			const float halfExtent = (index < largeCount)
										 ? random.NextFloat(LARGE_HALF_EXTENT_MIN, LARGE_HALF_EXTENT_MAX)
										 : random.NextFloat(SMALL_HALF_EXTENT_MIN, SMALL_HALF_EXTENT_MAX);

			bounds.push_back(MakeAabb(center, fang::Vector3{ halfExtent, halfExtent, halfExtent }));
		}

		return bounds;
	}


	/** @brief 組を番号の順に並べる。集合として比べるため。 */
	bool IsPairLess(const fang::ColliderPair& left, const fang::ColliderPair& right)
	{
		return (left.indexA != right.indexA) ? (left.indexA < right.indexA) : (left.indexB < right.indexB);
	}


	bool IsPairEqual(const fang::ColliderPair& left, const fang::ColliderPair& right)
	{
		return left.indexA == right.indexA && left.indexB == right.indexB;
	}


	/** @brief 総当たりで重なる組を全部集める。 */
	[[nodiscard]] std::vector<fang::ColliderPair> CollectPairsByBruteForce(const std::vector<fang::Aabb>& bounds)
	{
		std::vector<fang::ColliderPair> pairs;
		for (uint32_t indexA = 0; indexA < bounds.size(); ++indexA)
		{
			for (uint32_t indexB = indexA + 1; indexB < bounds.size(); ++indexB)
			{
				if (fang::OverlapsOnAllAxes(bounds[indexA], bounds[indexB]))
				{
					pairs.push_back(fang::ColliderPair{ .indexA = indexA, .indexB = indexB });
				}
			}
		}

		return pairs;
	}


	/** @brief 総当たりで領域クエリに重なる番号を全部集める。 */
	[[nodiscard]] std::vector<uint32_t> CollectIndicesByBruteForce(
		const std::vector<fang::Aabb>& bounds,
		const fang::Aabb&              queryBounds
	)
	{
		std::vector<uint32_t> indices;
		for (uint32_t index = 0; index < bounds.size(); ++index)
		{
			if (fang::OverlapsOnAllAxes(bounds[index], queryBounds))
			{
				indices.push_back(index);
			}
		}

		return indices;
	}


	/** @brief 登録の配置範囲の中に収まる、それなりの数が引っかかる領域クエリの箱。 */
	[[nodiscard]] fang::Aabb MakeQueryBoundsForMeasurement(uint32_t count)
	{
		const float halfRange = ComputePlacementHalfRange(count);

		return MakeAabb(
			fang::Vector3{ halfRange * 0.1f, 0.0f, halfRange * 0.1f },
			fang::Vector3{ halfRange * 0.5f, halfRange * 0.5f, halfRange * 0.5f }
		);
	}


	/** @brief 呼ばれた回数と要求バイトの合計を数えるアロケータ。実体は Heap。 */
	class ByteCountingAllocator final : public fang::IAllocator
	{
	public:
		[[nodiscard]] const char* GetName() const override { return "ByteCounting"; }

		[[nodiscard]] uint32_t GetAllocationCount() const { return m_allocationCount; }
		[[nodiscard]] uint32_t GetLiveAllocationCount() const { return m_liveAllocationCount; }
		[[nodiscard]] size_t   GetTotalRequestedBytes() const { return m_totalRequestedBytes; }


	public:
		[[nodiscard]] void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override
		{
			++m_allocationCount;
			++m_liveAllocationCount;
			m_totalRequestedBytes += size;

			return fang::HeapAllocator::GetInstance().Allocate(size, alignment);
		}

		void Deallocate(void* memory) override
		{
			if (memory != nullptr)
			{
				--m_liveAllocationCount;
			}

			fang::HeapAllocator::GetInstance().Deallocate(memory);
		}


	private:
		uint32_t m_allocationCount     = 0;
		uint32_t m_liveAllocationCount = 0;
		size_t   m_totalRequestedBytes = 0;
	};


	/** @brief 関数を呼ぶのにかかった秒を測る。 */
	template <typename Function> [[nodiscard]] float MeasureSeconds(Function&& function)
	{
		const auto start = std::chrono::steady_clock::now();
		function();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}
} // namespace


TEST_CASE("3 実装の候補と領域クエリが 4 段×2 分布で総当たりとも互いとも一致する")
{
	for (const uint32_t count : REGISTERED_COUNTS)
	{
		for (const EnDistribution distribution : { EnDistribution::Uniform, EnDistribution::Mixed })
		{
			CAPTURE(count);
			CAPTURE(doctest::String(NameOf(distribution)));

			const std::vector<fang::Aabb> bounds      = MakeBoundsForMeasurement(count, distribution);
			const fang::Aabb              queryBounds = MakeQueryBoundsForMeasurement(count);

			std::vector<fang::ColliderPair> expectedPairs = CollectPairsByBruteForce(bounds);
			std::sort(expectedPairs.begin(), expectedPairs.end(), IsPairLess);

			std::vector<uint32_t> expectedIndices = CollectIndicesByBruteForce(bounds, queryBounds);
			std::sort(expectedIndices.begin(), expectedIndices.end());

			for (const fang::EnBroadphaseType type : ALL_BROADPHASE_TYPES)
			{
				fang::IBroadphase* broadphase = fang::CreateBroadphase(fang::HeapAllocator::GetInstance(), type);
				if (broadphase == nullptr)
				{
					CHECK_MESSAGE(false, "Broadphase を作れなかった");
					continue;
				}
				CAPTURE(doctest::String(broadphase->GetName()));

				if (!broadphase->Initialize(fang::HeapAllocator::GetInstance(), count))
				{
					CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
					fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
					continue;
				}
				broadphase->Build(bounds);

				std::vector<fang::ColliderPair> actualPairs(PAIR_BUFFER_CAPACITY);
				const uint32_t                  pairCount = broadphase->CollectPairs(actualPairs);
				CHECK(pairCount < PAIR_BUFFER_CAPACITY); // 打ち切りが起きていない。
				actualPairs.resize(pairCount);
				std::sort(actualPairs.begin(), actualPairs.end(), IsPairLess);

				CHECK(actualPairs.size() == expectedPairs.size());
				if (actualPairs.size() == expectedPairs.size())
				{
					CHECK(std::equal(actualPairs.begin(), actualPairs.end(), expectedPairs.begin(), IsPairEqual));
				}

				std::vector<uint32_t> actualIndices(count);
				const uint32_t        indexCount = broadphase->QueryAabb(queryBounds, actualIndices);
				CHECK(indexCount < count + 1); // 打ち切りが起きていない(登録数を超えて返ることは無い)。
				actualIndices.resize(indexCount);
				std::sort(actualIndices.begin(), actualIndices.end());

				CHECK(actualIndices == expectedIndices);

				broadphase->Shutdown();
				fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
			}
		}
	}
}


TEST_CASE("3 実装は 2 分布とも 2 回連続で Build しても同じ組を返す")
{
	// 既定を決める基準と同じ登録数で見る。
	constexpr uint32_t REPRESENTATIVE_COUNT = 256;

	for (const EnDistribution distribution : { EnDistribution::Uniform, EnDistribution::Mixed })
	{
		CAPTURE(doctest::String(NameOf(distribution)));

		const std::vector<fang::Aabb> bounds = MakeBoundsForMeasurement(REPRESENTATIVE_COUNT, distribution);

		for (const fang::EnBroadphaseType type : ALL_BROADPHASE_TYPES)
		{
			fang::IBroadphase* broadphase = fang::CreateBroadphase(fang::HeapAllocator::GetInstance(), type);
			if (broadphase == nullptr)
			{
				CHECK_MESSAGE(false, "Broadphase を作れなかった");
				continue;
			}
			CAPTURE(doctest::String(broadphase->GetName()));

			if (!broadphase->Initialize(fang::HeapAllocator::GetInstance(), REPRESENTATIVE_COUNT))
			{
				CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
				fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
				continue;
			}

			std::vector<fang::ColliderPair> firstPairs(PAIR_BUFFER_CAPACITY);
			broadphase->Build(bounds);
			const uint32_t firstCount = broadphase->CollectPairs(firstPairs);

			std::vector<fang::ColliderPair> secondPairs(PAIR_BUFFER_CAPACITY);
			broadphase->Build(bounds);
			const uint32_t secondCount = broadphase->CollectPairs(secondPairs);

			CHECK(firstCount == secondCount);
			for (uint32_t index = 0; index < firstCount && index < secondCount; ++index)
			{
				CHECK(IsPairEqual(firstPairs[index], secondPairs[index]));
			}

			broadphase->Shutdown();
			fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
		}
	}
}


TEST_CASE("3 実装は退化した箱でも落ちない")
{
	for (const fang::EnBroadphaseType type : ALL_BROADPHASE_TYPES)
	{
		fang::IBroadphase* broadphase = fang::CreateBroadphase(fang::HeapAllocator::GetInstance(), type);
		if (broadphase == nullptr)
		{
			CHECK_MESSAGE(false, "Broadphase を作れなかった");
			continue;
		}
		CAPTURE(doctest::String(broadphase->GetName()));

		if (!broadphase->Initialize(fang::HeapAllocator::GetInstance(), 16))
		{
			CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
			fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
			continue;
		}

		std::vector<fang::ColliderPair> pairs(64);
		std::vector<uint32_t>           indices(64);

		// 登録 0 件。
		broadphase->Build(std::span<const fang::Aabb>{});
		CHECK(broadphase->CollectPairs(pairs) == 0);

		// 登録 1 件、大きさ 0 の箱。自分自身への領域クエリでは見つかる。
		const std::vector<fang::Aabb> singleZeroSize{
			MakeAabb(fang::Vector3{ 1.0f, 2.0f, 3.0f }, fang::Vector3{ 0.0f, 0.0f, 0.0f })
		};
		broadphase->Build(singleZeroSize);
		CHECK(broadphase->CollectPairs(pairs) == 0);
		CHECK(broadphase->QueryAabb(singleZeroSize[0], indices) == 1);

		// 極端に大きい箱と通常の箱が重なる。
		std::vector<fang::Aabb> hugeAndNormal;
		hugeAndNormal.push_back(MakeAabb(fang::Vector3{}, fang::Vector3{ 1.0e5f, 1.0e5f, 1.0e5f }));
		hugeAndNormal.push_back(MakeAabb(fang::Vector3{}, fang::Vector3{ 10.0f, 10.0f, 10.0f }));
		broadphase->Build(hugeAndNormal);
		CHECK(broadphase->CollectPairs(pairs) == 1);

		// 原点から遠く離れた箱(±10^6)。自分自身への領域クエリでは見つかる。
		const std::vector<fang::Aabb> farAway{
			MakeAabb(fang::Vector3{ 1.0e6f, -1.0e6f, 1.0e6f }, fang::Vector3{ 5.0f, 5.0f, 5.0f })
		};
		broadphase->Build(farAway);
		CHECK(broadphase->CollectPairs(pairs) == 0);
		CHECK(broadphase->QueryAabb(farAway[0], indices) == 1);

		// 全部が 1 点。組の数は書き込み先の上限で頭打ちになるだけ(16 個 = 120 組)。
		std::vector<fang::Aabb> allAtOnePoint;
		for (int index = 0; index < 16; ++index)
		{
			allAtOnePoint.push_back(MakeAabb(fang::Vector3{ 5.0f, 5.0f, 5.0f }, fang::Vector3{ 1.0f, 1.0f, 1.0f }));
		}
		broadphase->Build(allAtOnePoint);
		std::vector<fang::ColliderPair> smallPairBuffer(10);
		CHECK(broadphase->CollectPairs(smallPairBuffer) == 10);
		std::vector<fang::ColliderPair> fullPairBuffer(256);
		CHECK(broadphase->CollectPairs(fullPairBuffer) == 120);

		// 上限 16 に対して 30 個。超えたぶんを捨てるだけで落ちない。
		std::vector<fang::Aabb> overLimit;
		for (int index = 0; index < 30; ++index)
		{
			overLimit.push_back(MakeAabb(
				fang::Vector3{ static_cast<float>(index) * 0.01f, 0.0f, 0.0f },
				fang::Vector3{ 5.0f, 5.0f, 5.0f }
			));
		}
		broadphase->Build(overLimit);
		(void)broadphase->CollectPairs(pairs);

		broadphase->Shutdown();
		fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
	}
}


TEST_CASE("24 通りの Build + CollectPairs の所要時間")
{
	MESSAGE("=== Build + CollectPairs 所要時間(5 回中の最小値) ===");

	for (const uint32_t count : REGISTERED_COUNTS)
	{
		for (const EnDistribution distribution : { EnDistribution::Uniform, EnDistribution::Mixed })
		{
			const std::vector<fang::Aabb> bounds = MakeBoundsForMeasurement(count, distribution);

			for (const fang::EnBroadphaseType type : ALL_BROADPHASE_TYPES)
			{
				fang::IBroadphase* broadphase = fang::CreateBroadphase(fang::HeapAllocator::GetInstance(), type);
				if (broadphase == nullptr)
				{
					CHECK_MESSAGE(false, "Broadphase を作れなかった");
					continue;
				}
				if (!broadphase->Initialize(fang::HeapAllocator::GetInstance(), count))
				{
					CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
					fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
					continue;
				}

				std::vector<fang::ColliderPair> pairBuffer(PAIR_BUFFER_CAPACITY);

				float bestSeconds = FLT_MAX;
				for (int trial = 0; trial < 5; ++trial)
				{
					const float seconds = MeasureSeconds([&]() {
						broadphase->Build(bounds);
						(void)broadphase->CollectPairs(pairBuffer);
					});
					bestSeconds         = std::min(bestSeconds, seconds);
				}

				MESSAGE(
					"登録=",
					count,
					" 分布=",
					doctest::String(NameOf(distribution)),
					" 実装=",
					doctest::String(broadphase->GetName()),
					" : ",
					bestSeconds * 1000.0f,
					"ms (x6.39=",
					bestSeconds * 1000.0f * fang::budget::MEASURED_CPU_SCALE_FACTOR,
					"ms)"
				);

				broadphase->Shutdown();
				fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
			}
		}
	}
}


TEST_CASE("3 実装×4 段の確保量")
{
	MESSAGE("=== Initialize の要求バイト合計 ===");

	for (const uint32_t count : REGISTERED_COUNTS)
	{
		for (const fang::EnBroadphaseType type : ALL_BROADPHASE_TYPES)
		{
			ByteCountingAllocator allocator;

			fang::IBroadphase* broadphase = fang::CreateBroadphase(allocator, type);
			if (broadphase == nullptr)
			{
				CHECK_MESSAGE(false, "Broadphase を作れなかった");
				continue;
			}
			if (!broadphase->Initialize(allocator, count))
			{
				CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
				fang::DestroyBroadphase(allocator, broadphase);
				continue;
			}

			MESSAGE(
				"登録=",
				count,
				" 実装=",
				doctest::String(broadphase->GetName()),
				" : ",
				allocator.GetTotalRequestedBytes(),
				" バイト"
			);

			broadphase->Shutdown();

			fang::DestroyBroadphase(allocator, broadphase);
			CHECK(allocator.GetLiveAllocationCount() == 0);
		}
	}
}


TEST_CASE("3 実装は 100 フレーム回してもヒープ確保が増えない")
{
	constexpr uint32_t FRAME_REGISTERED_COUNT = 256;

	for (const fang::EnBroadphaseType type : ALL_BROADPHASE_TYPES)
	{
		ByteCountingAllocator allocator;

		fang::IBroadphase* broadphase = fang::CreateBroadphase(allocator, type);
		if (broadphase == nullptr)
		{
			CHECK_MESSAGE(false, "Broadphase を作れなかった");
			continue;
		}
		CAPTURE(doctest::String(broadphase->GetName()));

		if (!broadphase->Initialize(allocator, FRAME_REGISTERED_COUNT))
		{
			CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
			fang::DestroyBroadphase(allocator, broadphase);
			continue;
		}
		const uint32_t allocationCountAfterInitialize = allocator.GetAllocationCount();

		const std::vector<fang::Aabb> bounds = MakeBoundsForMeasurement(FRAME_REGISTERED_COUNT, EnDistribution::Mixed);
		const fang::Aabb              queryBounds = MakeQueryBoundsForMeasurement(FRAME_REGISTERED_COUNT);

		std::vector<fang::ColliderPair> pairs(4096);
		std::vector<uint32_t>           indices(FRAME_REGISTERED_COUNT);

		for (int frame = 0; frame < 100; ++frame)
		{
			broadphase->Build(bounds);
			(void)broadphase->CollectPairs(pairs);
			(void)broadphase->QueryAabb(queryBounds, indices);
		}

		CHECK(allocator.GetAllocationCount() == allocationCountAfterInitialize);

		broadphase->Shutdown();
		fang::DestroyBroadphase(allocator, broadphase);
	}
}


TEST_CASE("既定の Broadphase は登録 256 の 2 分布とも予算の 1 割に収まる見込み")
{
	// CollisionWorldDesc の既定値をそのまま使う ➡ タスク 6 で既定を変えたら自動で追従する。
	const fang::EnBroadphaseType defaultType = fang::CollisionWorldDesc{}.broadphaseType;

	for (const EnDistribution distribution : { EnDistribution::Uniform, EnDistribution::Mixed })
	{
		CAPTURE(doctest::String(NameOf(distribution)));

		const std::vector<fang::Aabb> bounds = MakeBoundsForMeasurement(256, distribution);

		fang::IBroadphase* broadphase = fang::CreateBroadphase(fang::HeapAllocator::GetInstance(), defaultType);
		if (broadphase == nullptr)
		{
			CHECK_MESSAGE(false, "Broadphase を作れなかった");
			continue;
		}
		if (!broadphase->Initialize(fang::HeapAllocator::GetInstance(), 256))
		{
			CHECK_MESSAGE(false, "Broadphase を初期化できなかった");
			fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
			continue;
		}

		std::vector<fang::ColliderPair> pairBuffer(PAIR_BUFFER_CAPACITY);
		const float                     measuredSeconds = MeasureSeconds([&]() {
			broadphase->Build(bounds);
			(void)broadphase->CollectPairs(pairBuffer);
		});

		const float scaledSeconds = measuredSeconds * fang::budget::MEASURED_CPU_SCALE_FACTOR;

		MESSAGE(
			"既定=",
			doctest::String(broadphase->GetName()),
			" 256 ",
			doctest::String(NameOf(distribution)),
			" : ",
			measuredSeconds * 1000.0f,
			"ms (x6.39=",
			scaledSeconds * 1000.0f,
			"ms)"
		);

#if !FANG_DEBUG
		// 16.6ms の 1 割 = 1.66ms。6.39 倍は Preview/Release の最適化前提の実測値なので Debug では見ない。
		CHECK(scaledSeconds <= fang::budget::FRAME_BUDGET_SECONDS * 0.1f);
#else
		CHECK(scaledSeconds >= 0.0f);
#endif

		broadphase->Shutdown();
		fang::DestroyBroadphase(fang::HeapAllocator::GetInstance(), broadphase);
	}
}
