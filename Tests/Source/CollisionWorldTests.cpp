/**
 * @file CollisionWorldTests.cpp
 * @brief CollisionWorld のテスト。接触の作り直し、上限、更新中のヒープ確保 0、レイキャストと球の重なり。
 */
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include <doctest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>


namespace
{
	/** @brief 呼ばれた回数を数えるだけのアロケータ。更新のたびのヒープ確保が 0 であることの確認に使う。 */
	class CountingAllocator final : public fang::IAllocator
	{
	public:
		[[nodiscard]] const char* GetName() const override { return "Counting"; }

		/** @brief Allocate が呼ばれた回数。 */
		[[nodiscard]] uint32_t GetAllocationCount() const { return m_allocationCount; }


	public:
		[[nodiscard]] void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override
		{
			++m_allocationCount;

			return fang::HeapAllocator::GetInstance().Allocate(size, alignment);
		}

		void Deallocate(void* memory) override { fang::HeapAllocator::GetInstance().Deallocate(memory); }


	private:
		uint32_t m_allocationCount = 0;
	};


	/** @brief 球のコライダーを 1 つ作る。 */
	fang::ColliderProxy MakeSphereProxy(const fang::Vector3& center, float radius, uint32_t userIndex)
	{
		return fang::ColliderProxy{
			.shape     = fang::MakeColliderShape(fang::Sphere{ .center = center, .radius = radius }),
			.userIndex = userIndex,
		};
	}


	/** @brief 軸平行な箱のコライダーを 1 つ作る。 */
	fang::ColliderProxy MakeBoxProxy(const fang::Vector3& center, float halfExtent, uint32_t userIndex)
	{
		return fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::OBB{ .center = center, .halfExtents = { halfExtent, halfExtent, halfExtent } }
			),
			.userIndex = userIndex,
		};
	}


	/** @brief userIndex の組が接触に含まれているか。並びは問わない。 */
	bool HasContactBetween(std::span<const fang::Contact> contacts, uint32_t first, uint32_t second)
	{
		for (const fang::Contact& contact : contacts)
		{
			if ((contact.userIndexA == first && contact.userIndexB == second) ||
				(contact.userIndexA == second && contact.userIndexB == first))
			{
				return true;
			}
		}

		return false;
	}


	/** @brief 決定的な擬似乱数。3 実装の一致テストの結果が実行ごとに変わらないよう、標準の乱数を使わない。 */
	class TestRandom
	{
	public:
		[[nodiscard]] float NextFloat(float minimum, float maximum)
		{
			m_state = m_state * 1664525u + 1013904223u;

			return minimum + (maximum - minimum) * (static_cast<float>(m_state >> 8) / 16777216.0f);
		}


	private:
		uint32_t m_state = 777u;
	};


	/** @brief 球と OBB を混ぜた、重なりを含む配置。3 実装の結果が一致することを見るのに使う。 */
	std::vector<fang::ColliderProxy> MakeMixedProxiesForCrossImplementationTest()
	{
		TestRandom                       random;
		std::vector<fang::ColliderProxy> proxies;
		for (uint32_t index = 0; index < 40; ++index)
		{
			const fang::Vector3 center{ random.NextFloat(-30.0f, 30.0f),
										random.NextFloat(-5.0f, 5.0f),
										random.NextFloat(-30.0f, 30.0f) };

			proxies.push_back(
				(index % 2 == 0) ? MakeSphereProxy(center, 3.0f, index) : MakeBoxProxy(center, 3.0f, index)
			);
		}

		return proxies;
	}
} // namespace


TEST_CASE("重なった組だけが接触として返る")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 100 番と 200 番が重なり、300 番だけ離れている。
	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 0.0f, 0.0f, 0.0f }, 2.0f, 100));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 3.0f, 0.0f, 0.0f }, 2.0f, 200));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 50.0f, 0.0f, 0.0f }, 2.0f, 300));

	world.Update(proxies);

	CHECK(world.GetColliderCount() == 3);
	CHECK(world.GetContacts().size() == 1);
	CHECK(HasContactBetween(world.GetContacts(), 100, 200));

	const fang::Contact& contact = world.GetContacts()[0];
	CHECK(contact.depth == doctest::Approx(1.0f));
	CHECK(fang::Length(contact.normal) == doctest::Approx(1.0f));

	// 離した次のフレームでは接触が消える（前のフレームの結果を持ち越さない）。
	proxies[1] = MakeSphereProxy(fang::Vector3{ 30.0f, 0.0f, 0.0f }, 2.0f, 200);
	world.Update(proxies);
	CHECK(world.GetContacts().size() == 0);

	world.Shutdown();
}


TEST_CASE("登録 0 件でも更新とクエリで落ちない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	world.Update(std::span<const fang::ColliderProxy>{});

	CHECK(world.GetColliderCount() == 0);
	CHECK(world.GetContacts().size() == 0);

	fang::RayHit hit;
	CHECK_FALSE(world.Raycast(fang::Vector3{}, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 100.0f, fang::QueryFilter{}, &hit));

	std::vector<uint32_t> indices(8);
	CHECK(
		world.OverlapSphere(fang::Sphere{ .center = fang::Vector3{}, .radius = 10.0f }, fang::QueryFilter{}, indices) ==
		0
	);

	world.Shutdown();

	// 二重に呼んでも安全。
	world.Shutdown();
}


TEST_CASE("登録と接触の上限を超えたぶんは捨てて落ちない")
{
	// 全部が重なる配置にして、コライダーも接触も上限に当てる。
	const fang::CollisionWorldDesc desc{ .maxColliderCount = 4, .maxPairCount = 8, .maxContactCount = 2 };

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), desc));

	std::vector<fang::ColliderProxy> proxies;
	for (uint32_t index = 0; index < 10; ++index)
	{
		proxies.push_back(MakeSphereProxy(fang::Vector3{ static_cast<float>(index) * 0.1f, 0.0f, 0.0f }, 2.0f, index));
	}

	world.Update(proxies);

	// 上限 4 まで受け取り、4 個から作れる 6 組のうち 2 件で打ち切る。
	CHECK(world.GetColliderCount() == 4);
	CHECK(world.GetContacts().size() == 2);

	world.Shutdown();
}


TEST_CASE("上限に 0 が混じった CollisionWorld は初期化に失敗する")
{
	fang::CollisionWorld world;
	CHECK_FALSE(world.Initialize(
		fang::HeapAllocator::GetInstance(),
		fang::CollisionWorldDesc{ .maxColliderCount = 0, .maxPairCount = 8, .maxContactCount = 8 }
	));
}


TEST_CASE("更新のたびのヒープ確保が 0")
{
	CountingAllocator allocator;

	fang::CollisionWorld world;
	CHECK(world.Initialize(
		allocator,
		fang::CollisionWorldDesc{ .maxColliderCount = 64, .maxPairCount = 256, .maxContactCount = 256 }
	));

	const uint32_t allocationCountAfterInitialize = allocator.GetAllocationCount();
	CHECK(allocationCountAfterInitialize > 0);

	std::vector<fang::ColliderProxy> proxies;
	for (uint32_t index = 0; index < 32; ++index)
	{
		proxies.push_back(MakeSphereProxy(fang::Vector3{ static_cast<float>(index), 0.0f, 0.0f }, 1.5f, index));
	}

	for (int frame = 0; frame < 100; ++frame)
	{
		world.Update(proxies);

		fang::RayHit hit;
		(void)world.Raycast(
			fang::Vector3{ -100.0f, 0.0f, 0.0f },
			fang::Vector3{ 1.0f, 0.0f, 0.0f },
			500.0f,
			fang::QueryFilter{},
			&hit
		);

		uint32_t indices[8]{};
		(void)world
			.OverlapSphere(fang::Sphere{ .center = fang::Vector3{}, .radius = 5.0f }, fang::QueryFilter{}, indices);
	}

	// Initialize の後は 1 回も増えない。
	CHECK(allocator.GetAllocationCount() == allocationCountAfterInitialize);

	world.Shutdown();
}


TEST_CASE("掃引と視線を 1000 回回してもヒープ確保が増えない")
{
	CountingAllocator allocator;

	fang::CollisionWorld world;
	CHECK(world.Initialize(
		allocator,
		fang::CollisionWorldDesc{ .maxColliderCount = 64, .maxPairCount = 256, .maxContactCount = 256 }
	));

	std::vector<fang::ColliderProxy> proxies;
	for (uint32_t index = 0; index < 32; ++index)
	{
		proxies.push_back(MakeSphereProxy(fang::Vector3{ static_cast<float>(index), 0.0f, 0.0f }, 1.5f, index));
	}
	world.Update(proxies);

	const uint32_t allocationCountAfterUpdate = allocator.GetAllocationCount();

	for (int iteration = 0; iteration < 1000; ++iteration)
	{
		fang::SweepHit sweepHits[8];
		(void)world.SweepSphere(
			fang::Sphere{ .center = fang::Vector3{ -100.0f, 0.0f, 0.0f }, .radius = 1.5f },
			fang::Vector3{ 500.0f, 0.0f, 0.0f },
			fang::QueryFilter{},
			sweepHits
		);

		fang::RayHit blockingHit;
		(void)world.HasLineOfSight(
			fang::Vector3{ -100.0f, 0.0f, 0.0f },
			fang::Vector3{ 400.0f, 0.0f, 0.0f },
			fang::QueryFilter{},
			&blockingHit
		);
	}

	CHECK(allocator.GetAllocationCount() == allocationCountAfterUpdate);

	world.Shutdown();
}


TEST_CASE("レイキャストが 3 つの形すべてに当たる")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 1.0f, 10));
	proxies.push_back(MakeBoxProxy(fang::Vector3{ 0.0f, 10.0f, 0.0f }, 1.0f, 20));
	proxies.push_back(
		fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::Capsule{ .pointA = { 0.0f, 0.0f, 8.0f }, .pointB = { 0.0f, 4.0f, 8.0f }, .radius = 1.0f }
			),
			.userIndex = 30,
		}
	);

	world.Update(proxies);

	const fang::Vector3 origin{ 0.0f, 0.0f, 0.0f };

	fang::RayHit hit;

	// 球。表面までの距離は 10 - 1 = 9。
	CHECK(world.Raycast(origin, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.userIndex == 10);
	CHECK(hit.distance == doctest::Approx(9.0f));
	CHECK(hit.normal.x == doctest::Approx(-1.0f));

	// OBB。下面までの距離は 10 - 1 = 9。
	CHECK(world.Raycast(origin, fang::Vector3{ 0.0f, 1.0f, 0.0f }, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.userIndex == 20);
	CHECK(hit.distance == doctest::Approx(9.0f));
	CHECK(hit.normal.y == doctest::Approx(-1.0f));

	// カプセル。側面までの距離は 8 - 1 = 7。
	CHECK(world.Raycast(origin, fang::Vector3{ 0.0f, 0.0f, 1.0f }, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.userIndex == 30);
	CHECK(hit.distance == doctest::Approx(7.0f));
	CHECK(hit.normal.z == doctest::Approx(-1.0f));

	// 何も無い向き。
	CHECK_FALSE(world.Raycast(origin, fang::Vector3{ -1.0f, 0.0f, 0.0f }, 100.0f, fang::QueryFilter{}, &hit));

	// 届かない長さ。
	CHECK_FALSE(world.Raycast(origin, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 5.0f, fang::QueryFilter{}, &hit));

	world.Shutdown();
}


TEST_CASE("始点が形の中なら距離 0 を返す")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	const fang::Vector3 direction{ 1.0f, 0.0f, 0.0f };
	fang::RayHit        hit;

	std::vector<fang::ColliderProxy> proxies;

	proxies.assign({ MakeSphereProxy(fang::Vector3{}, 5.0f, 1) });
	world.Update(proxies);
	CHECK(world.Raycast(fang::Vector3{}, direction, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.distance == doctest::Approx(0.0f));
	CHECK(hit.normal.x == doctest::Approx(-1.0f));

	proxies.assign({ MakeBoxProxy(fang::Vector3{}, 5.0f, 2) });
	world.Update(proxies);
	CHECK(world.Raycast(fang::Vector3{}, direction, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.distance == doctest::Approx(0.0f));

	proxies.assign(
		{ fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::Capsule{ .pointA = { 0.0f, -5.0f, 0.0f }, .pointB = { 0.0f, 5.0f, 0.0f }, .radius = 2.0f }
			),
			.userIndex = 3,
		} }
	);
	world.Update(proxies);
	CHECK(world.Raycast(fang::Vector3{}, direction, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.distance == doctest::Approx(0.0f));

	world.Shutdown();
}


TEST_CASE("いちばん近いものだけがレイキャストの結果になる")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 手前 ➡ 奥の順で並べず、登録の順と距離の順をわざと食い違わせる。
	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 30.0f, 0.0f, 0.0f }, 1.0f, 30));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 1.0f, 10));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 20.0f, 0.0f, 0.0f }, 1.0f, 20));

	world.Update(proxies);

	fang::RayHit hit;
	CHECK(world.Raycast(fang::Vector3{}, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 100.0f, fang::QueryFilter{}, &hit));
	CHECK(hit.userIndex == 10);
	CHECK(hit.distance == doctest::Approx(9.0f));

	world.Shutdown();
}


TEST_CASE("球の重なりが範囲内の番号を全部返す")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, 0));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 5.0f, 0.0f, 0.0f }, 1.0f, 1));
	proxies.push_back(MakeBoxProxy(fang::Vector3{ 0.0f, 5.0f, 0.0f }, 1.0f, 2));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 100.0f, 0.0f, 0.0f }, 1.0f, 3));

	world.Update(proxies);

	// 半径 6 なら 0・1・2 が入り、100 の位置にある 3 は入らない。
	std::vector<uint32_t> indices(8);
	const uint32_t        count =
		world.OverlapSphere(fang::Sphere{ .center = fang::Vector3{}, .radius = 6.0f }, fang::QueryFilter{}, indices);
	CHECK(count == 3);

	bool hasFarOne = false;
	for (uint32_t index = 0; index < count; ++index)
	{
		hasFarOne = hasFarOne || (indices[index] == 3);
	}
	CHECK_FALSE(hasFarOne);

	// 書き込み先が足りなければ、そこで打ち切る。
	std::vector<uint32_t> smallIndices(1);
	CHECK(
		world.OverlapSphere(
			fang::Sphere{ .center = fang::Vector3{}, .radius = 6.0f },
			fang::QueryFilter{},
			smallIndices
		) == 1
	);

	// 誰にも届かない範囲。
	CHECK(
		world.OverlapSphere(
			fang::Sphere{ .center = fang::Vector3{ 0.0f, -50.0f, 0.0f }, .radius = 1.0f },
			fang::QueryFilter{},
			indices
		) == 0
	);

	world.Shutdown();
}


TEST_CASE("layerMask で絞り込んだレイキャストは対象外の登録を無視する")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t WALL_LAYER      = 1u << 0;
	constexpr uint32_t CHARACTER_LAYER = 1u << 1;

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape     = fang::MakeColliderShape(fang::Sphere{ .center = { 10.0f, 0.0f, 0.0f }, .radius = 1.0f }),
			.userIndex = 1,
			.layerMask = CHARACTER_LAYER,
		}
	);
	world.Update(proxies);

	fang::RayHit hit;

	// 壁だけを見るフィルタでは、キャラの層しか無いこの登録には当たらない。
	CHECK_FALSE(world.Raycast(
		fang::Vector3{},
		fang::Vector3{ 1.0f, 0.0f, 0.0f },
		100.0f,
		fang::QueryFilter{ .layerMask = WALL_LAYER },
		&hit
	));

	// キャラの層を見るフィルタなら当たる。
	CHECK(world.Raycast(
		fang::Vector3{},
		fang::Vector3{ 1.0f, 0.0f, 0.0f },
		100.0f,
		fang::QueryFilter{ .layerMask = CHARACTER_LAYER },
		&hit
	));
	CHECK(hit.userIndex == 1);

	world.Shutdown();
}


TEST_CASE("excludedUserIndices で除外した番号はレイキャストに出ない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 1.0f, 42));
	world.Update(proxies);

	const uint32_t excluded[] = { 42 };
	fang::RayHit   hit;

	CHECK_FALSE(world.Raycast(
		fang::Vector3{},
		fang::Vector3{ 1.0f, 0.0f, 0.0f },
		100.0f,
		fang::QueryFilter{ .excludedUserIndices = excluded },
		&hit
	));

	CHECK(world.Raycast(fang::Vector3{}, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 100.0f, fang::QueryFilter{}, &hit));

	world.Shutdown();
}


TEST_CASE("layerMask で絞り込んだ球の重なりは対象外の登録を無視する")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t WALL_LAYER      = 1u << 0;
	constexpr uint32_t CHARACTER_LAYER = 1u << 1;

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape     = fang::MakeColliderShape(fang::Sphere{ .center = { 0.0f, 0.0f, 0.0f }, .radius = 1.0f }),
			.userIndex = 7,
			.layerMask = CHARACTER_LAYER,
		}
	);
	world.Update(proxies);

	std::vector<uint32_t> indices(8);
	CHECK(
		world.OverlapSphere(
			fang::Sphere{ .center = fang::Vector3{}, .radius = 5.0f },
			fang::QueryFilter{ .layerMask = WALL_LAYER },
			indices
		) == 0
	);
	CHECK(
		world.OverlapSphere(
			fang::Sphere{ .center = fang::Vector3{}, .radius = 5.0f },
			fang::QueryFilter{ .layerMask = CHARACTER_LAYER },
			indices
		) == 1
	);

	world.Shutdown();
}


TEST_CASE("excludedUserIndices で除外した番号は球の重なりに出ない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{}, 1.0f, 5));
	proxies.push_back(MakeSphereProxy(fang::Vector3{}, 1.0f, 9));
	world.Update(proxies);

	const uint32_t        excluded[] = { 5 };
	std::vector<uint32_t> indices(8);
	const uint32_t        count = world.OverlapSphere(
		fang::Sphere{ .center = fang::Vector3{}, .radius = 5.0f },
		fang::QueryFilter{ .excludedUserIndices = excluded },
		indices
	);
	CHECK(count == 1);
	CHECK(indices[0] == 9);

	world.Shutdown();
}


TEST_CASE("球の掃引が近い順に並んで返る")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 登録の順と距離の順をわざと食い違わせる。
	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 30.0f, 0.0f, 0.0f }, 1.0f, 30));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 1.0f, 10));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 20.0f, 0.0f, 0.0f }, 1.0f, 20));
	world.Update(proxies);

	std::vector<fang::SweepHit> hits(8);
	const fang::SweepResult     result = world.SweepSphere(
		fang::Sphere{ .center = fang::Vector3{}, .radius = 1.0f },
		fang::Vector3{ 100.0f, 0.0f, 0.0f },
		fang::QueryFilter{},
		hits
	);

	CHECK(result.hitCount == 3);
	CHECK_FALSE(result.isTruncated);
	CHECK(hits[0].userIndex == 10);
	CHECK(hits[1].userIndex == 20);
	CHECK(hits[2].userIndex == 30);
	CHECK(hits[0].timeRatio < hits[1].timeRatio);
	CHECK(hits[1].timeRatio < hits[2].timeRatio);

	world.Shutdown();
}


TEST_CASE("掃引の書き込み先が足りないぶんは遠いほうから捨てる")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 30.0f, 0.0f, 0.0f }, 1.0f, 30));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 1.0f, 10));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 20.0f, 0.0f, 0.0f }, 1.0f, 20));
	world.Update(proxies);

	std::vector<fang::SweepHit> hits(2);
	const fang::SweepResult     result = world.SweepSphere(
		fang::Sphere{ .center = fang::Vector3{}, .radius = 1.0f },
		fang::Vector3{ 100.0f, 0.0f, 0.0f },
		fang::QueryFilter{},
		hits
	);

	CHECK(result.hitCount == 2);
	CHECK(result.isTruncated);
	CHECK(hits[0].userIndex == 10);
	CHECK(hits[1].userIndex == 20);

	world.Shutdown();
}


TEST_CASE("layerMask で絞り込んだ掃引は対象外の登録を無視する")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t WALL_LAYER      = 1u << 0;
	constexpr uint32_t CHARACTER_LAYER = 1u << 1;

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape     = fang::MakeColliderShape(fang::Sphere{ .center = { 10.0f, 0.0f, 0.0f }, .radius = 1.0f }),
			.userIndex = 1,
			.layerMask = CHARACTER_LAYER,
		}
	);
	world.Update(proxies);

	std::vector<fang::SweepHit> hits(4);

	fang::SweepResult wallResult = world.SweepSphere(
		fang::Sphere{ .center = fang::Vector3{}, .radius = 1.0f },
		fang::Vector3{ 100.0f, 0.0f, 0.0f },
		fang::QueryFilter{ .layerMask = WALL_LAYER },
		hits
	);
	CHECK(wallResult.hitCount == 0);

	fang::SweepResult characterResult = world.SweepSphere(
		fang::Sphere{ .center = fang::Vector3{}, .radius = 1.0f },
		fang::Vector3{ 100.0f, 0.0f, 0.0f },
		fang::QueryFilter{ .layerMask = CHARACTER_LAYER },
		hits
	);
	CHECK(characterResult.hitCount == 1);
	CHECK(hits[0].userIndex == 1);

	world.Shutdown();
}


TEST_CASE("視線は間を遮る登録があれば false、無ければ true になる")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 5.0f, 0.0f, 0.0f }, 1.0f, 1));
	world.Update(proxies);

	fang::RayHit blockingHit;

	// 遮る登録がある。
	CHECK_FALSE(
		world.HasLineOfSight(fang::Vector3{}, fang::Vector3{ 10.0f, 0.0f, 0.0f }, fang::QueryFilter{}, &blockingHit)
	);
	CHECK(blockingHit.userIndex == 1);

	// 手前で止まらない向きなら遮られない。
	CHECK(world.HasLineOfSight(fang::Vector3{}, fang::Vector3{ 0.0f, 10.0f, 0.0f }, fang::QueryFilter{}, &blockingHit));

	world.Shutdown();
}


TEST_CASE("視線は発信元と対象自身を除外すれば通る")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 発信元(番号 1)と対象(番号 2)の位置それぞれにも登録がある(狼自身・対象自身の当たり)。
	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, 1));
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 10.0f, 0.0f, 0.0f }, 1.0f, 2));
	world.Update(proxies);

	fang::RayHit blockingHit;

	// 除外しなければ、発信元自身の登録に当たって遮られる。
	CHECK_FALSE(
		world.HasLineOfSight(fang::Vector3{}, fang::Vector3{ 10.0f, 0.0f, 0.0f }, fang::QueryFilter{}, &blockingHit)
	);

	// 発信元と対象を除外すれば、間に何も無いので通る。
	const uint32_t excluded[] = { 1, 2 };
	CHECK(world.HasLineOfSight(
		fang::Vector3{},
		fang::Vector3{ 10.0f, 0.0f, 0.0f },
		fang::QueryFilter{ .excludedUserIndices = excluded },
		&blockingHit
	));

	world.Shutdown();
}


TEST_CASE("視線を layerMask で壁だけに絞ると、キャラの層は遮らない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t WALL_LAYER      = 1u << 0;
	constexpr uint32_t CHARACTER_LAYER = 1u << 1;

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape     = fang::MakeColliderShape(fang::Sphere{ .center = { 5.0f, 0.0f, 0.0f }, .radius = 1.0f }),
			.userIndex = 3,
			.layerMask = CHARACTER_LAYER,
		}
	);
	world.Update(proxies);

	fang::RayHit blockingHit;

	CHECK_FALSE(
		world.HasLineOfSight(fang::Vector3{}, fang::Vector3{ 10.0f, 0.0f, 0.0f }, fang::QueryFilter{}, &blockingHit)
	);

	CHECK(world.HasLineOfSight(
		fang::Vector3{},
		fang::Vector3{ 10.0f, 0.0f, 0.0f },
		fang::QueryFilter{ .layerMask = WALL_LAYER },
		&blockingHit
	));

	world.Shutdown();
}


TEST_CASE("3 実装とも外から見える結果が変わらない")
{
	constexpr fang::EnBroadphaseType types[fang::BROADPHASE_TYPE_COUNT] = {
		fang::EnBroadphaseType::SweepAndPrune,
		fang::EnBroadphaseType::UniformGrid,
		fang::EnBroadphaseType::DynamicAabbTree,
	};
	constexpr const char* expectedNames[fang::BROADPHASE_TYPE_COUNT] = {
		"SweepAndPrune",
		"UniformGrid",
		"DynamicAabbTree",
	};

	const std::vector<fang::ColliderProxy> proxies = MakeMixedProxiesForCrossImplementationTest();

	std::vector<std::pair<uint32_t, uint32_t>> expectedContactPairs;
	std::vector<uint32_t>                      expectedOverlapIndices;
	std::vector<uint32_t>                      expectedSweepUserIndices;
	bool                                       expectedHasRayHit       = false;
	uint32_t                                   expectedRayHitUserIndex = 0;
	bool                                       expectedHasLineOfSight  = false;

	for (uint32_t typeIndex = 0; typeIndex < fang::BROADPHASE_TYPE_COUNT; ++typeIndex)
	{
		fang::CollisionWorld           world;
		const fang::CollisionWorldDesc desc{ .broadphaseType = types[typeIndex] };
		CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), desc));
		CAPTURE(expectedNames[typeIndex]);

		CHECK(std::strcmp(world.GetBroadphaseName(), expectedNames[typeIndex]) == 0);

		world.Update(proxies);

		std::vector<std::pair<uint32_t, uint32_t>> contactPairs;
		for (const fang::Contact& contact : world.GetContacts())
		{
			contactPairs.push_back(
				{ std::min(contact.userIndexA, contact.userIndexB), std::max(contact.userIndexA, contact.userIndexB) }
			);
		}
		std::sort(contactPairs.begin(), contactPairs.end());

		fang::RayHit rayHit;
		const bool   hasRayHit = world.Raycast(
			fang::Vector3{ -100.0f, 0.0f, 0.0f },
			fang::Vector3{ 1.0f, 0.0f, 0.0f },
			300.0f,
			fang::QueryFilter{},
			&rayHit
		);

		std::vector<uint32_t> overlapIndices(64);
		const uint32_t        overlapCount = world.OverlapSphere(
			fang::Sphere{ .center = fang::Vector3{}, .radius = 20.0f },
			fang::QueryFilter{},
			overlapIndices
		);
		overlapIndices.resize(overlapCount);
		std::sort(overlapIndices.begin(), overlapIndices.end());

		std::vector<fang::SweepHit> sweepHits(64);
		const fang::SweepResult     sweepResult = world.SweepSphere(
			fang::Sphere{ .center = fang::Vector3{ -100.0f, 0.0f, 0.0f }, .radius = 2.0f },
			fang::Vector3{ 200.0f, 0.0f, 0.0f },
			fang::QueryFilter{},
			sweepHits
		);
		std::vector<uint32_t> sweepUserIndices;
		for (uint32_t hitIndex = 0; hitIndex < sweepResult.hitCount; ++hitIndex)
		{
			sweepUserIndices.push_back(sweepHits[hitIndex].userIndex);
		}

		fang::RayHit blockingHit;
		const bool   hasLineOfSight = world.HasLineOfSight(
			fang::Vector3{ -100.0f, 0.0f, 0.0f },
			fang::Vector3{ 100.0f, 0.0f, 0.0f },
			fang::QueryFilter{},
			&blockingHit
		);

		if (typeIndex == 0)
		{
			expectedContactPairs     = contactPairs;
			expectedOverlapIndices   = overlapIndices;
			expectedSweepUserIndices = sweepUserIndices;
			expectedHasRayHit        = hasRayHit;
			expectedRayHitUserIndex  = hasRayHit ? rayHit.userIndex : 0;
			expectedHasLineOfSight   = hasLineOfSight;

			// 重なりも掃引も 1 件も無いと、実装の違いに気付けない。
			CHECK(expectedContactPairs.size() > 0);
			CHECK(expectedOverlapIndices.size() > 0);
			CHECK(expectedSweepUserIndices.size() > 0);
		}
		else
		{
			CHECK(contactPairs == expectedContactPairs);
			CHECK(overlapIndices == expectedOverlapIndices);
			CHECK(sweepUserIndices == expectedSweepUserIndices);
			CHECK(hasRayHit == expectedHasRayHit);
			if (hasRayHit)
			{
				CHECK(rayHit.userIndex == expectedRayHitUserIndex);
			}
			CHECK(hasLineOfSight == expectedHasLineOfSight);
		}

		world.Shutdown();
	}
}


TEST_CASE("同じ位置への視線は常に見える扱いになる")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::RayHit blockingHit;
	CHECK(world.HasLineOfSight(
		fang::Vector3{ 5.0f, 0.0f, 0.0f },
		fang::Vector3{ 5.0f, 0.0f, 0.0f },
		fang::QueryFilter{},
		&blockingHit
	));

	world.Shutdown();
}
