/**
 * @file CollisionBudgetTests.cpp
 * @brief Collision の実機予算のテスト。256 登録 + 視線 32 本 + 掃引 4 本を PC で測り、6.39 倍で確かめる
 *        (PlatformBudgetTests と同じ流儀。Xbox が無くても実機の 1 割予算に収まる見込みを確認する)。
 */
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Core/Platform/Budget.h"
#include <doctest.h>
#include <chrono>
#include <vector>


namespace
{
	constexpr uint32_t REGISTERED_COLLIDER_COUNT = 256;
	constexpr uint32_t LINE_OF_SIGHT_QUERY_COUNT = 32;
	constexpr uint32_t SWEEP_QUERY_COUNT         = 4;

	/** @brief 決定的な擬似乱数。測定結果が実行ごとに変わらないよう、標準の乱数を使わない。 */
	class TestRandom
	{
	public:
		[[nodiscard]] float NextFloat(float minimum, float maximum)
		{
			m_state = m_state * 1664525u + 1013904223u;
			return minimum + (maximum - minimum) * (static_cast<float>(m_state >> 8) / 16777216.0f);
		}


	private:
		uint32_t m_state = 54321u;
	};


	/** @brief 実機の雑魚と置き物を模した、散らばった登録を作る。 */
	std::vector<fang::ColliderProxy> MakeScatteredProxies(uint32_t count)
	{
		TestRandom                       random;
		std::vector<fang::ColliderProxy> proxies;
		proxies.reserve(count);

		for (uint32_t index = 0; index < count; ++index)
		{
			const fang::Vector3 center{ random.NextFloat(-200.0f, 200.0f),
										random.NextFloat(0.0f, 30.0f),
										random.NextFloat(-200.0f, 200.0f) };

			// 3 形を均等に混ぜる。狼はカプセル、置き物は OBB、それ以外の当たりは球を想定。
			switch (index % 3)
			{
				case 0:
					proxies.push_back(
						fang::ColliderProxy{
							.shape     = fang::MakeColliderShape(fang::Sphere{ .center = center, .radius = 40.0f }),
							.userIndex = index,
						}
					);
					break;

				case 1:
					proxies.push_back(
						fang::ColliderProxy{
							.shape = fang::MakeColliderShape(
								fang::Capsule{
									.pointA = center,
									.pointB = center + fang::Vector3{ 0.0f, 100.0f, 0.0f },
									.radius = 20.0f,
								}
							),
							.userIndex = index,
						}
					);
					break;

				default:
					proxies.push_back(
						fang::ColliderProxy{
							.shape = fang::MakeColliderShape(
								fang::OBB{ .center = center, .halfExtents = { 40.0f, 40.0f, 40.0f } }
							),
							.userIndex = index,
						}
					);
					break;
			}
		}

		return proxies;
	}


	/** @brief 関数を呼ぶのにかかった秒を測る。 */
	template <typename Function> [[nodiscard]] float MeasureSeconds(Function&& function)
	{
		const auto start = std::chrono::steady_clock::now();
		function();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}
} // namespace


TEST_CASE("256 登録・視線 32 本・掃引 4 本が実機予算の 1 割に収まる見込み")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	const std::vector<fang::ColliderProxy> proxies = MakeScatteredProxies(REGISTERED_COLLIDER_COUNT);
	world.Update(proxies);

	const float measuredSeconds = MeasureSeconds([&world]() {
		for (uint32_t index = 0; index < LINE_OF_SIGHT_QUERY_COUNT; ++index)
		{
			const fang::Vector3 fromPosition{ static_cast<float>(index) - 16.0f, 5.0f, 0.0f };
			const fang::Vector3 toPosition{ static_cast<float>(index) - 16.0f, 5.0f, 150.0f };

			fang::RaycastHit blockingHit;
			(void)world.HasLineOfSight(fromPosition, toPosition, fang::QueryFilter{}, &blockingHit);
		}

		for (uint32_t index = 0; index < SWEEP_QUERY_COUNT; ++index)
		{
			const fang::Vector3 start{ static_cast<float>(index) * 10.0f - 15.0f, 5.0f, -150.0f };

			fang::SweepHit hits[8];
			(void)world.SweepSphere(
				fang::Sphere{ .center = start, .radius = 20.0f },
				fang::Vector3{ 0.0f, 0.0f, 300.0f },
				fang::QueryFilter{},
				hits
			);
		}
	});

	const float scaledSeconds = measuredSeconds * fang::budget::MEASURED_CPU_SCALE_FACTOR;

#if !FANG_DEBUG
	// 16.6ms の 1 割 = 1.66ms。6.39 倍は Preview/Release の最適化前提の実測値なので Debug では見ない。
	CHECK(scaledSeconds <= fang::budget::FRAME_BUDGET_SECONDS * 0.1f);
#else
	CHECK(scaledSeconds >= 0.0f);
#endif

	world.Shutdown();
}
