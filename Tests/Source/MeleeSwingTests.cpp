/**
 * @file MeleeSwingTests.cpp
 * @brief MeleeSwing のテスト。振りの時間割、二重ヒットの抑止、すり抜け配置の捕捉、除外・間合い、
 *        書き込み先を超えたときの打ち切りを確かめる。
 */
#include "Collision/Collision.h"
#include "Core/Memory/Allocator.h"
#include "Scene/MeleeSwing.h"
#include <doctest.h>
#include <vector>


namespace
{
	constexpr float FRAME_SECONDS = 1.0f / 60.0f;

	constexpr uint32_t SELF_USER_INDEX = 999;

	constexpr uint32_t TEST_LAYER_CHARACTER = 1u << 0;
	constexpr uint32_t TEST_LAYER_PROP      = 1u << 1;
	constexpr uint32_t TEST_LAYER_ENEMY     = 1u << 2;


	/** @brief 立ち位置は原点、向きは +X 固定。振りの発生源を毎回書かずに済むための既定入力。 */
	[[nodiscard]] fang::MeleeSwingInput MakeInput(
		bool     attackButtonDown,
		uint32_t targetLayerMask = fang::ALL_COLLISION_LAYERS
	)
	{
		return fang::MeleeSwingInput{
			.selfPosition       = fang::Vector3{},
			.selfFacingRadians  = 0.0f,
			.isAttackButtonDown = attackButtonDown,
			.selfUserIndex      = SELF_USER_INDEX,
			.targetLayerMask    = targetLayerMask,
		};
	}


	void RegisterTarget(
		std::vector<fang::ColliderProxy>& proxies,
		uint32_t                          userIndex,
		const fang::Vector3&              center,
		float                             radius,
		uint32_t                          layerMask = fang::ALL_COLLISION_LAYERS
	)
	{
		proxies.push_back(
			fang::ColliderProxy{
				.shape     = fang::MakeColliderShape(fang::Sphere{ .center = center, .radius = radius }),
				.userIndex = userIndex,
				.layerMask = layerMask,
			}
		);
	}


	/** @brief 判定区間の k 番目（0 始まり）の掃引が終わったときの牙の位置。 */
	[[nodiscard]] fang::Vector3 FangPositionAtStep(const fang::MeleeSwingParams& params, uint32_t stepIndex)
	{
		return fang::ComputeFangPosition(params, fang::Vector3{}, 0.0f, static_cast<float>(stepIndex) / 9.0f);
	}
} // namespace


TEST_CASE("MeleeSwing: 判定区間の間だけ掃引が飛び、構え・戻り・待機は0本")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::MeleeSwingParams params{};
	fang::MeleeSwingState  state{};

	uint32_t sweepCount = 0;

	// 立ち上がり ➡ 構え(6) ➡ 判定(9) ➡ 戻り(12) ➡ 待機、で余裕を持って 40 フレーム回す。
	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const bool                   attackDown = (frame == 0);
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(attackDown), FRAME_SECONDS, &state, hits);

		if (result.didSweep)
		{
			++sweepCount;
		}
	}

	// 判定区間の秒数(0.15s)を 1/60 で割った本数だけ掃引が飛ぶ。
	CHECK(sweepCount == 9);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 判定区間の秒数を倍にすると掃引本数も倍")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::MeleeSwingParams params{};
	params.activeSeconds = 0.30f; // 既定の 2 倍。

	fang::MeleeSwingState state{};
	uint32_t              sweepCount = 0;

	for (int frame = 0; frame < 60; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		if (result.didSweep)
		{
			++sweepCount;
		}
	}

	CHECK(sweepCount == 18);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 押しっぱなしでも新しい振りは1回しか始まらない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::MeleeSwingParams params{};
	fang::MeleeSwingState  state{};

	uint32_t startCount = 0;

	// ボタンを一度も離さないまま、戻りを終えて Ready へ戻った後まで回す。
	for (int frame = 0; frame < 50; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(true), FRAME_SECONDS, &state, hits);

		if (result.didStartSwing)
		{
			++startCount;
		}
	}

	CHECK(startCount == 1);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 弧の上に並べた3体に1回の振りで当たる")
{
	fang::MeleeSwingParams params{};

	std::vector<fang::ColliderProxy> proxies;
	RegisterTarget(proxies, 1, FangPositionAtStep(params, 1), 5.0f);
	RegisterTarget(proxies, 2, FangPositionAtStep(params, 4), 5.0f);
	RegisterTarget(proxies, 3, FangPositionAtStep(params, 7), 5.0f);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		totalNewHitCount += result.newHitCount;
	}

	CHECK(totalNewHitCount == 3);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 同じ振りの間、同じ相手には1回だけ当たる")
{
	fang::MeleeSwingParams params{};

	// この位置は、ある1フレームの終点であると同時に、次のフレームの始点でもある
	// (始点で重なっていれば timeRatio = 0 で当たる) ➡ 記録が無ければ2回当たってしまう配置。
	const fang::Vector3 targetPosition = FangPositionAtStep(params, 4);

	std::vector<fang::ColliderProxy> proxies;
	RegisterTarget(proxies, 1, targetPosition, 5.0f);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		totalNewHitCount += result.newHitCount;
	}

	CHECK(totalNewHitCount == 1);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 振りを終えて次の振りを始めると同じ相手にまた当たる")
{
	fang::MeleeSwingParams params{};

	const fang::Vector3 targetPosition = FangPositionAtStep(params, 4);

	std::vector<fang::ColliderProxy> proxies;
	RegisterTarget(proxies, 1, targetPosition, 5.0f);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};

	uint32_t firstSwingHitCount = 0;
	for (int frame = 0; frame < 30; ++frame) // Ready へ戻るまで(6+9+12=27)。
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		firstSwingHitCount += result.newHitCount;
	}
	CHECK(firstSwingHitCount == 1);
	CHECK(state.phase == fang::EnMeleeSwingPhase::Ready);

	uint32_t secondSwingHitCount = 0;
	for (int frame = 0; frame < 30; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		secondSwingHitCount += result.newHitCount;
	}
	CHECK(secondSwingHitCount == 1);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 始点でも終点でも重なっていない相手を拾う")
{
	fang::MeleeSwingParams params{};
	params.fangRadiusCentimeters = 1.0f; // 牙を細くし、弦の途中でしか触れない配置を作る。

	constexpr uint32_t  STEP_INDEX           = 4;
	const fang::Vector3 previousFangPosition = FangPositionAtStep(params, STEP_INDEX - 1);
	const fang::Vector3 currentFangPosition  = FangPositionAtStep(params, STEP_INDEX);
	const fang::Vector3 chordMidpoint        = (previousFangPosition + currentFangPosition) * 0.5f;

	constexpr float TARGET_RADIUS = 10.0f;

	// 前提: 始点・終点のどちらからも、細くした牙(半径1)との合計より遠い。
	CHECK(fang::Length(chordMidpoint - previousFangPosition) > params.fangRadiusCentimeters + TARGET_RADIUS);
	CHECK(fang::Length(chordMidpoint - currentFangPosition) > params.fangRadiusCentimeters + TARGET_RADIUS);

	std::vector<fang::ColliderProxy> proxies;
	RegisterTarget(proxies, 1, chordMidpoint, TARGET_RADIUS);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		totalNewHitCount += result.newHitCount;
	}

	CHECK(totalNewHitCount == 1);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 自分自身・味方・置き物には当たらない")
{
	fang::MeleeSwingParams params{};
	const fang::Vector3    targetPosition = FangPositionAtStep(params, 4);

	std::vector<fang::ColliderProxy> proxies;
	RegisterTarget(proxies, SELF_USER_INDEX, targetPosition, 5.0f);         // 自分自身。
	RegisterTarget(proxies, 2, targetPosition, 5.0f, TEST_LAYER_CHARACTER); // 味方。
	RegisterTarget(proxies, 3, targetPosition, 5.0f, TEST_LAYER_PROP);      // 置き物。

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0, TEST_LAYER_ENEMY), FRAME_SECONDS, &state, hits);

		totalNewHitCount += result.newHitCount;
	}

	CHECK(totalNewHitCount == 0);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 間合いの外の相手には当たらない")
{
	fang::MeleeSwingParams params{};

	const fang::Vector3 farAwayPosition{ params.reachCentimeters * 2.0f, params.fangHeightCentimeters, 0.0f };

	std::vector<fang::ColliderProxy> proxies;
	RegisterTarget(proxies, 1, farAwayPosition, 5.0f);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		totalNewHitCount += result.newHitCount;
	}

	CHECK(totalNewHitCount == 0);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 当たった数が書き込み先を超えても落ちず、8で頭打ちになる")
{
	fang::MeleeSwingParams params{};
	const fang::Vector3    clusterCenter = FangPositionAtStep(params, 4);

	std::vector<fang::ColliderProxy> proxies;
	for (uint32_t index = 0; index < 12; ++index)
	{
		// ほぼ同じ位置に 12 体重ねる。牙(半径40)の中に全部収まる散らばりにする。
		const fang::Vector3 offset{ static_cast<float>(index) * 0.5f, 0.0f, 0.0f };
		RegisterTarget(proxies, index, clusterCenter + offset, 5.0f);
	}

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;
	bool                  sawTruncated     = false;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		totalNewHitCount += result.newHitCount;
		sawTruncated = sawTruncated || result.isTruncated;
	}

	CHECK(totalNewHitCount == fang::MAX_MELEE_SWING_HIT_COUNT);
	CHECK(sawTruncated);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 判定区間0秒でも落ちず、掃引0本でRecoveryへ抜ける")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::MeleeSwingParams params{};
	params.activeSeconds = 0.0f;

	fang::MeleeSwingState state{};
	uint32_t              sweepCount = 0;

	// 構え(既定 0.10s)を抜けるまで回す。抜けた瞬間、判定区間(0秒)を素通りして戻りへ入る。
	for (int frame = 0; frame < 8; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		if (result.didSweep)
		{
			++sweepCount;
		}
	}

	CHECK(sweepCount == 0);
	CHECK(state.phase == fang::EnMeleeSwingPhase::Recovery);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 3区間すべて0秒でも無限ループせず1フレームでReadyへ抜ける")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::MeleeSwingParams params{};
	params.windUpSeconds   = 0.0f;
	params.activeSeconds   = 0.0f;
	params.recoverySeconds = 0.0f;

	fang::MeleeSwingState state{};
	fang::SweepHit        hits[fang::MAX_MELEE_SWING_HIT_COUNT];

	const fang::MeleeSwingResult result =
		fang::StepMeleeSwing(world, params, MakeInput(true), FRAME_SECONDS, &state, hits);

	CHECK_FALSE(result.didSweep);
	CHECK(state.phase == fang::EnMeleeSwingPhase::Ready);

	world.Shutdown();
}


TEST_CASE("MeleeSwing: 振りの記録が満杯だと、複数フレームにまたがる新顔もダメージ無しで落ちる")
{
	fang::MeleeSwingParams params{};

	std::vector<fang::ColliderProxy> proxies;
	for (uint32_t step = 0; step < fang::MAX_MELEE_SWING_HIT_COUNT; ++step)
	{
		// 8 体を、判定区間の最初の 8 フレームへ 1 体ずつ当たるように並べる。
		RegisterTarget(proxies, step, FangPositionAtStep(params, step), 5.0f);
	}
	// 9 体目は最後のフレーム(8 番目)にだけ当たる、まったく新しい相手。
	constexpr uint32_t NINTH_TARGET_USER_INDEX = 100;
	RegisterTarget(proxies, NINTH_TARGET_USER_INDEX, FangPositionAtStep(params, 8), 5.0f);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};

	std::vector<uint32_t> allHitUserIndices;
	bool                  sawTruncated = false;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, params, MakeInput(frame == 0), FRAME_SECONDS, &state, hits);

		for (uint32_t hitIndex = 0; hitIndex < result.newHitCount; ++hitIndex)
		{
			allHitUserIndices.push_back(hits[hitIndex].userIndex);
		}
		sawTruncated = sawTruncated || result.isTruncated;
	}

	CHECK(allHitUserIndices.size() == fang::MAX_MELEE_SWING_HIT_COUNT);
	CHECK(sawTruncated);

	bool foundNinthTarget = false;
	for (const uint32_t userIndex : allHitUserIndices)
	{
		if (userIndex == NINTH_TARGET_USER_INDEX)
		{
			foundNinthTarget = true;
		}
	}
	CHECK_FALSE(foundNinthTarget);

	world.Shutdown();
}
