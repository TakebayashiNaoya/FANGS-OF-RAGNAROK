/**
 * @file CameraOcclusionTests.cpp
 * @brief カメラの遮蔽解決(SolveCameraOcclusion)の完了条件を、視線が通るかと距離の数値で確かめる。
 * @details CollisionWorld を直接組み立て、置き物は TEST_ATTRIBUTE_PROP を付けた OBB で置く
 *          (WolfDefeatTests.cpp と同じ流儀)。
 */
#include "Collision/Collision.h"
#include "Core/Memory/Allocator.h"
#include "Scene/CameraOcclusion.h"
#include <doctest.h>
#include <cmath>
#include <vector>


namespace
{
	constexpr uint32_t TEST_ATTRIBUTE_PROP      = 1u << 0;
	constexpr uint32_t TEST_ATTRIBUTE_CHARACTER = 1u << 1;
	constexpr uint32_t TEST_ATTRIBUTE_WOLF      = 1u << 2;

	constexpr fang::Vector3 DEFAULT_TARGET{ 0.0f, 0.0f, 0.0f };
	constexpr fang::Vector3 DEFAULT_EYE{ 350.0f, 0.0f, 0.0f }; // distance 350、+X 方向。

	/** @brief 実物(CameraFollowParameter)と同じ既定値。blockerAttributeMask だけ置き物に絞る。 */
	[[nodiscard]] fang::CameraOcclusionParameter MakeParameter()
	{
		return fang::CameraOcclusionParameter{ .blockerAttributeMask = TEST_ATTRIBUTE_PROP };
	}

	/** @brief 注視点から既定の視点への線の上、xPosition の位置に置き物を 1 つ足す。 */
	void AddPropOnAxis(
		std::vector<fang::ColliderProxy>& proxies,
		float                             xPosition,
		uint32_t                          attributeMask = TEST_ATTRIBUTE_PROP
	)
	{
		proxies.push_back(
			fang::ColliderProxy{
				.shape = fang::MakeColliderShape(
					fang::OBB{
						.center      = fang::Vector3{ xPosition, 0.0f, 0.0f },
						.halfExtents = fang::Vector3{ 30.0f, 100.0f, 100.0f },
					}
				),
				.userIndex     = 1,
				.attributeMask = attributeMask,
			}
		);
	}

	/** @brief 呼ばれた回数を数えるアロケータ。実体は Heap(BroadphaseComparisonTests.cpp と同じ流儀)。 */
	class AllocationCountingAllocator final : public fang::IAllocator
	{
	public:
		[[nodiscard]] const char* GetName() const override { return "AllocationCounting"; }

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
} // namespace


TEST_CASE("CameraOcclusion: 遮蔽の後、解いた視点から注視点への視線が通る")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 250.0f);
	world.Update(proxies);

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK(result.didHitBlocker);

	fang::RaycastHit        blockingHit;
	const fang::QueryFilter filter{ .attributeMask = TEST_ATTRIBUTE_PROP };
	CHECK(world.HasLineOfSight(result.eyePosition, DEFAULT_TARGET, filter, &blockingHit));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 解いた視点が注視点と既定の視点を結ぶ線分の上に載る")
{
	const fang::Vector3 target{ 50.0f, 30.0f, -20.0f };
	const fang::Vector3 defaultEye = target + fang::Vector3{ 280.0f, 140.0f, 70.0f };

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::Sphere{ .center = target + (defaultEye - target) * 0.6f, .radius = 90.0f }
			),
			.userIndex     = 1,
			.attributeMask = TEST_ATTRIBUTE_PROP,
		}
	);
	world.Update(proxies);

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = target, .defaultEyePosition = defaultEye }
	);

	CHECK(result.didHitBlocker);
	CHECK(result.distanceCentimeters > 0.0f);
	CHECK(result.distanceCentimeters < fang::Length(defaultEye - target));

	const float crossLength = fang::Length(fang::Cross(result.eyePosition - target, defaultEye - target));
	CHECK(crossLength == doctest::Approx(0.0f).epsilon(1.0e-2));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 遮蔽が無ければ既定の視点・距離のまま")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK_FALSE(result.didHitBlocker);
	CHECK(result.distanceCentimeters == doctest::Approx(350.0f));
	CHECK(result.eyePosition.x == doctest::Approx(DEFAULT_EYE.x));
	CHECK(result.eyePosition.y == doctest::Approx(DEFAULT_EYE.y));
	CHECK(result.eyePosition.z == doctest::Approx(DEFAULT_EYE.z));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 寄せに2フレーム以上かけない(1回の呼び出しで解き切る)")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 250.0f);
	world.Update(proxies);

	// 前フレームは既定の350cmだった、という持ち越し。寄せは即時なので速度制限は掛からない。
	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{
			.targetPosition              = DEFAULT_TARGET,
			.defaultEyePosition          = DEFAULT_EYE,
			.previousDistanceCentimeters = 350.0f,
			.deltaTimeSeconds            = 1.0f / 60.0f,
		}
	);

	CHECK(result.distanceCentimeters < 300.0f);

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 解いた距離は常に下限(150cm)以上")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 50.0f);
	world.Update(proxies);

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK(result.distanceCentimeters == doctest::Approx(150.0f));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 下限に貼り付いてなお遮られる配置では遮蔽を残す")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 50.0f);
	world.Update(proxies);

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK(result.distanceCentimeters == doctest::Approx(150.0f));

	fang::RaycastHit        blockingHit;
	const fang::QueryFilter filter{ .attributeMask = TEST_ATTRIBUTE_PROP };
	CHECK_FALSE(world.HasLineOfSight(result.eyePosition, DEFAULT_TARGET, filter, &blockingHit));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 遮蔽が外れた後、1フレームの伸び幅が速度制限を超えない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{}); // 遮蔽が外れた状態。

	const fang::CameraOcclusionParameter parameter    = MakeParameter();
	const float                          deltaSeconds = 1.0f / 60.0f;

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		parameter,
		fang::CameraOcclusionInput{
			.targetPosition              = DEFAULT_TARGET,
			.defaultEyePosition          = DEFAULT_EYE,
			.previousDistanceCentimeters = 150.0f,
			.deltaTimeSeconds            = deltaSeconds,
		}
	);

	const float maximumStep = parameter.returnSpeedCentimetersPerSecond * deltaSeconds;
	CHECK(result.distanceCentimeters <= 150.0f + maximumStep + 1.0e-3f);

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 350cmから150cmまで寄せた状態で遮蔽を外すと0.5秒で350cmに戻る")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::CameraOcclusionParameter parameter    = MakeParameter();
	const float                          deltaSeconds = 1.0f / 60.0f;

	float distance = 150.0f;
	for (int frame = 0; frame < 30; ++frame)
	{
		const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
			&world,
			parameter,
			fang::CameraOcclusionInput{
				.targetPosition              = DEFAULT_TARGET,
				.defaultEyePosition          = DEFAULT_EYE,
				.previousDistanceCentimeters = distance,
				.deltaTimeSeconds            = deltaSeconds,
			}
		);
		distance = result.distanceCentimeters;

		if (frame == 28)
		{
			CHECK(distance < 350.0f - 0.1f);
		}
	}

	CHECK(distance == doctest::Approx(350.0f).epsilon(1.0e-3));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 遮蔽物が近いほど寄る")
{
	const auto solveWithPropAt = [](float xPosition) -> float {
		fang::CollisionWorld world;
		CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

		std::vector<fang::ColliderProxy> proxies;
		AddPropOnAxis(proxies, xPosition);
		world.Update(proxies);

		const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
			&world,
			MakeParameter(),
			fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
		);

		world.Shutdown();
		return result.distanceCentimeters;
	};

	CHECK(solveWithPropAt(200.0f) < solveWithPropAt(300.0f));
}


TEST_CASE("CameraOcclusion: 置き物0個でも落ちず、距離が既定のまま")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK(result.distanceCentimeters == doctest::Approx(350.0f));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: CollisionWorldが無くても落ちず、距離が既定のまま")
{
	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		nullptr,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK_FALSE(result.didHitBlocker);
	CHECK(result.distanceCentimeters == doctest::Approx(350.0f));
}


TEST_CASE("CameraOcclusion: 全滅中(注視点が動かない)でも落ちない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	float previousDistance = 0.0f;
	for (int frame = 0; frame < 2; ++frame)
	{
		const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
			&world,
			MakeParameter(),
			fang::CameraOcclusionInput{
				.targetPosition              = DEFAULT_TARGET,
				.defaultEyePosition          = DEFAULT_EYE,
				.previousDistanceCentimeters = previousDistance,
				.deltaTimeSeconds            = 1.0f / 60.0f,
			}
		);
		previousDistance = result.distanceCentimeters;
	}

	CHECK(previousDistance == doctest::Approx(350.0f));
}


TEST_CASE("CameraOcclusion: 狼・雑魚は遮蔽に数えない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 250.0f, TEST_ATTRIBUTE_CHARACTER | TEST_ATTRIBUTE_WOLF);
	world.Update(proxies);

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK_FALSE(result.didHitBlocker);
	CHECK(result.distanceCentimeters == doctest::Approx(350.0f));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 距離がNaN・0・負にならない(注視点が置き物の中)")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 0.0f); // 注視点(x=0)を包む配置。
	world.Update(proxies);

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
	);

	CHECK(std::isfinite(result.distanceCentimeters));
	CHECK(result.distanceCentimeters == doctest::Approx(150.0f));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 起動1フレーム目に戻しの速度制限が効いて短いまま出ない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::CameraOcclusionResult result = fang::SolveCameraOcclusion(
		&world,
		MakeParameter(),
		fang::CameraOcclusionInput{
			.targetPosition              = DEFAULT_TARGET,
			.defaultEyePosition          = DEFAULT_EYE,
			.previousDistanceCentimeters = 0.0f,
			.deltaTimeSeconds            = 1.0f / 60.0f,
		}
	);

	CHECK(result.distanceCentimeters == doctest::Approx(350.0f));

	world.Shutdown();
}


TEST_CASE("CameraOcclusion: 1フレームあたりのヒープ確保が0")
{
	AllocationCountingAllocator allocator;

	fang::CollisionWorld world;
	CHECK(world.Initialize(allocator, fang::CollisionWorldDesc{}));

	std::vector<fang::ColliderProxy> proxies;
	AddPropOnAxis(proxies, 250.0f);
	world.Update(proxies);

	const uint32_t allocationCountBeforeSolve = allocator.GetAllocationCount();

	for (int iteration = 0; iteration < 1000; ++iteration)
	{
		(void)fang::SolveCameraOcclusion(
			&world,
			MakeParameter(),
			fang::CameraOcclusionInput{ .targetPosition = DEFAULT_TARGET, .defaultEyePosition = DEFAULT_EYE }
		);
	}

	CHECK(allocator.GetAllocationCount() == allocationCountBeforeSolve);

	world.Shutdown();
}
