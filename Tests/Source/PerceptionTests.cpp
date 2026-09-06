/**
 * @file PerceptionTests.cpp
 * @brief Perception のテスト。距離・視野角・遮蔽の 3 段の足切りと、視線を投げた回数を確かめる。
 */
#include "AI/AI.h"
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include <doctest.h>
#include <vector>


namespace
{
	/** @brief 球のコライダーを 1 つ作る。 */
	fang::ColliderProxy MakeSphereProxy(const fang::Vector3& center, float radius, uint32_t userIndex)
	{
		return fang::ColliderProxy{
			.shape     = fang::MakeColliderShape(fang::Sphere{ .center = center, .radius = radius }),
			.userIndex = userIndex,
		};
	}
} // namespace


TEST_CASE("索敵距離・視野角・遮蔽がそろうと見える")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::PerceptionParameter parameter{};
	const fang::PerceptionInput     input{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ 500.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};

	const fang::PerceptionResult result = fang::Sense(world, parameter, input);
	CHECK(result.isVisible);
	CHECK(result.didTraceLineOfSight);
	CHECK(result.distanceCentimeters == doctest::Approx(500.0f));

	world.Shutdown();
}


TEST_CASE("視野角の外は距離が近くても見つけない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::PerceptionParameter parameter{};

	// 正面(+X)を向いているところへ、真後ろ(-X)に相手がいる。距離は十分近い。
	const fang::PerceptionInput input{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ -100.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};

	const fang::PerceptionResult result = fang::Sense(world, parameter, input);
	CHECK_FALSE(result.isVisible);

	world.Shutdown();
}


TEST_CASE("索敵距離の外は正面でも見つけない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	fang::PerceptionParameter parameter{};
	parameter.sightRangeCentimeters = 2000.0f;

	const fang::PerceptionInput input{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ 2500.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};

	const fang::PerceptionResult result = fang::Sense(world, parameter, input);
	CHECK_FALSE(result.isVisible);
	CHECK_FALSE(result.didTraceLineOfSight);

	world.Shutdown();
}


TEST_CASE("遮蔽物があると見つけない、どければ見つける")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 目の高さを 0 にして、視線を y = 0 の水平線にそろえる（置き物の高さと合わせるため）。
	fang::PerceptionParameter parameter{};
	parameter.eyeHeightCentimeters       = 0.0f;
	parameter.targetEyeHeightCentimeters = 0.0f;

	const fang::PerceptionInput input{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ 500.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};

	// 間に置き物を挟む。
	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(fang::Vector3{ 250.0f, 0.0f, 0.0f }, 30.0f, 100));
	world.Update(proxies);

	CHECK_FALSE(fang::Sense(world, parameter, input).isVisible);

	// どければ見える。
	world.Update(std::span<const fang::ColliderProxy>{});
	CHECK(fang::Sense(world, parameter, input).isVisible);

	world.Shutdown();
}


TEST_CASE("自分自身と相手自身は遮蔽物として数えない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 目の高さを 0 にする（自分・相手の当たり判定と同じ高さに視線をそろえ、除外が効いているかを実際に試す）。
	fang::PerceptionParameter parameter{};
	parameter.eyeHeightCentimeters       = 0.0f;
	parameter.targetEyeHeightCentimeters = 0.0f;

	const fang::PerceptionInput input{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ 500.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};

	// 自分と相手それぞれの当たり判定(狼やこの雑魚自身)が、視線の両端(視線を投げる起点そのもの)に登録されている。
	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(MakeSphereProxy(input.selfPosition, 40.0f, input.selfUserIndex));
	proxies.push_back(MakeSphereProxy(input.targetPosition, 40.0f, input.targetUserIndex));
	world.Update(proxies);

	CHECK(fang::Sense(world, parameter, input).isVisible);

	world.Shutdown();
}


TEST_CASE("視野角と索敵距離で落ちれば視線を投げない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(std::span<const fang::ColliderProxy>{});

	const fang::PerceptionParameter parameter{};

	// 視野角の外。
	const fang::PerceptionInput outsideCone{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ -500.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};
	CHECK_FALSE(fang::Sense(world, parameter, outsideCone).didTraceLineOfSight);

	// 索敵距離の内側かつ視野角の内側なら投げる。
	const fang::PerceptionInput inside{
		.selfPosition      = fang::Vector3{ 0.0f, 0.0f, 0.0f },
		.selfFacingRadians = 0.0f,
		.targetPosition    = fang::Vector3{ 500.0f, 0.0f, 0.0f },
		.selfUserIndex     = 1,
		.targetUserIndex   = 2,
	};
	CHECK(fang::Sense(world, parameter, inside).didTraceLineOfSight);

	world.Shutdown();
}
