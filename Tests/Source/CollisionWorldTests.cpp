/**
 * @file CollisionWorldTests.cpp
 * @brief CollisionWorld のテスト。接触の作り直し、上限、更新中のヒープ確保 0、レイキャストと球の重なり。
 */
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include <doctest.h>
#include <cmath>
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
