/**
 * @file WolfDefeatTests.cpp
 * @brief 雑魚の振りの合図(見えていて間合いの内)・停止距離からの到達・振り中の静止・当たる相手の絞り込みの
 *        合成テスト。Scene / Collision / AI だけを使い、Game を参照しない(ADR-033)。
 */
#include "AI/AI.h"
#include "Collision/Collision.h"
#include "Core/Math/MathConstants.h"
#include "Core/Memory/Allocator.h"
#include "Scene/MeleeSwing.h"
#include <doctest.h>
#include <cmath>
#include <vector>


namespace
{
	constexpr float FRAME_SECONDS = 1.0f / 60.0f;

	constexpr uint32_t ATTACKER_USER_INDEX = 999;
	constexpr uint32_t TARGET_USER_INDEX   = 1;

	constexpr uint32_t TEST_ATTRIBUTE_CHARACTER = 1u << 0;
	constexpr uint32_t TEST_ATTRIBUTE_PROP      = 1u << 1;
	constexpr uint32_t TEST_ATTRIBUTE_ENEMY     = 1u << 2;
	constexpr uint32_t TEST_ATTRIBUTE_WOLF      = 1u << 3;

	// EnemyParameter(設計)と同じ値。雑魚が狼へ詰める距離と牙の間合い。Tests は Game を参照しないので、
	// ここでも同じ数を持つ(設計の static_assert と合わせて 2 か所で縛られる)。
	constexpr float ENEMY_STOP_DISTANCE_CENTIMETERS = 120.0f;
	constexpr float ENEMY_REACH_CENTIMETERS         = 150.0f;

	/** @brief EnemyController が振りの合図に使うのと同じ式。 */
	[[nodiscard]] bool ComputeSwingTrigger(
		const fang::CollisionWorld&      world,
		const fang::PerceptionParameter& perceptionParameter,
		const fang::MeleeSwingParameter& swingParameter,
		const fang::Vector3&             selfPosition,
		float                            selfFacingRadians,
		const fang::Vector3&             targetPosition
	)
	{
		const fang::PerceptionInput perceptionInput{
			.selfPosition      = selfPosition,
			.selfFacingRadians = selfFacingRadians,
			.targetPosition    = targetPosition,
			.selfUserIndex     = ATTACKER_USER_INDEX,
			.targetUserIndex   = TARGET_USER_INDEX,
		};
		const fang::PerceptionResult perception = fang::Sense(world, perceptionParameter, perceptionInput);

		return perception.isVisible && perception.distanceCentimeters <= swingParameter.reachCentimeters;
	}
} // namespace


TEST_CASE("WolfDefeat: 見えていても間合いの外なら振りは始まらない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	const fang::PerceptionParameter perceptionParameter{};
	fang::MeleeSwingParameter       swingParameter{};
	swingParameter.reachCentimeters = ENEMY_REACH_CENTIMETERS;
	swingParameter.triggerMode      = fang::EnMeleeSwingTrigger::Continuous;

	// 間合いの2倍離れた位置。視線を遮るものは無いので見えてはいる。
	const fang::Vector3 targetPosition{ ENEMY_REACH_CENTIMETERS * 2.0f, 0.0f, 0.0f };

	fang::MeleeSwingState state{};
	bool                  didStartSwing = false;

	for (int frame = 0; frame < 40; ++frame)
	{
		const bool isAttackRequested =
			ComputeSwingTrigger(world, perceptionParameter, swingParameter, fang::Vector3{}, 0.0f, targetPosition);

		fang::SweepHit              hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingInput input{
			.selfPosition      = fang::Vector3{},
			.selfFacingRadians = 0.0f,
			.isAttackRequested = isAttackRequested,
			.selfUserIndex     = ATTACKER_USER_INDEX,
		};
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, swingParameter, input, FRAME_SECONDS, &state, hits);

		didStartSwing = didStartSwing || result.didStartSwing;
	}

	CHECK_FALSE(didStartSwing);

	world.Shutdown();
}


TEST_CASE("WolfDefeat: 間合いの内でも遮蔽の裏なら振りは始まらない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	const fang::PerceptionParameter perceptionParameter{ .blockerAttributeMask = TEST_ATTRIBUTE_PROP };
	fang::MeleeSwingParameter       swingParameter{};
	swingParameter.reachCentimeters = ENEMY_REACH_CENTIMETERS;
	swingParameter.triggerMode      = fang::EnMeleeSwingTrigger::Continuous;

	// 間合いの内側だが、途中に壁を置いて視線を遮る。
	const fang::Vector3 targetPosition{ ENEMY_REACH_CENTIMETERS * 0.5f, 0.0f, 0.0f };

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::OBB{
					.center      = fang::Vector3{ ENEMY_REACH_CENTIMETERS * 0.25f, 0.0f, 0.0f },
					.halfExtents = fang::Vector3{ 10.0f, 200.0f, 200.0f },
				}
			),
			.userIndex     = 2,
			.attributeMask = TEST_ATTRIBUTE_PROP,
		}
	);
	world.Update(proxies);

	fang::MeleeSwingState state{};
	bool                  didStartSwing = false;

	for (int frame = 0; frame < 40; ++frame)
	{
		const bool isAttackRequested =
			ComputeSwingTrigger(world, perceptionParameter, swingParameter, fang::Vector3{}, 0.0f, targetPosition);

		fang::SweepHit              hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingInput input{
			.selfPosition      = fang::Vector3{},
			.selfFacingRadians = 0.0f,
			.isAttackRequested = isAttackRequested,
			.selfUserIndex     = ATTACKER_USER_INDEX,
		};
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, swingParameter, input, FRAME_SECONDS, &state, hits);

		didStartSwing = didStartSwing || result.didStartSwing;
	}

	CHECK_FALSE(didStartSwing);

	world.Shutdown();
}


TEST_CASE("WolfDefeat: 追跡が止まる距離に居る狼に、向きによらず振りが届く")
{
	fang::PursuitParameter pursuitParameter{};
	pursuitParameter.stopDistanceCentimeters = ENEMY_STOP_DISTANCE_CENTIMETERS;

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = true;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ 1000.0f, 0.0f, 0.0f };

	fang::EnPursuitState state    = fang::EnPursuitState::Idle;
	fang::Vector3        position = fang::Vector3{};

	// 十分な時間をかけて停止距離まで詰める(PursuitStateMachineTests と同じ流儀)。
	for (int step = 0; step < 600; ++step)
	{
		const fang::MoveIntent intent =
			fang::StepPursuit(pursuitParameter, blackboard, position, FRAME_SECONDS, &state);
		position += intent.desiredDelta;
	}

	const float stoppedDistance = blackboard.lastSeenTargetPosition.x - position.x;
	CHECK(stoppedDistance <= ENEMY_STOP_DISTANCE_CENTIMETERS + 0.5f);

	fang::MeleeSwingParameter swingParameter{};
	swingParameter.reachCentimeters = ENEMY_REACH_CENTIMETERS;

	// 狼役のカプセル(体長204・半径40)を4方向へ向けて、どの向きでも当たることを確かめる。
	// 中心の高さは牙の高さ(fangHeightCentimeters)に合わせる ➡ 牙の球と確実に重なる。
	constexpr float BODY_HALF_LENGTH_CENTIMETERS = 100.0f;
	constexpr float BODY_RADIUS_CENTIMETERS      = 40.0f;

	const fang::Vector3 targetCenter =
		blackboard.lastSeenTargetPosition + fang::Vector3{ 0.0f, swingParameter.fangHeightCentimeters, 0.0f };

	for (int orientationIndex = 0; orientationIndex < 4; ++orientationIndex)
	{
		const float         facingRadians = static_cast<float>(orientationIndex) * (fang::PI * 0.5f);
		const fang::Vector3 axis{
			std::cos(facingRadians) * BODY_HALF_LENGTH_CENTIMETERS,
			0.0f,
			std::sin(facingRadians) * BODY_HALF_LENGTH_CENTIMETERS,
		};

		std::vector<fang::ColliderProxy> proxies;
		proxies.push_back(
			fang::ColliderProxy{
				.shape = fang::MakeColliderShape(
					fang::Capsule{
						.pointA = targetCenter - axis,
						.pointB = targetCenter + axis,
						.radius = BODY_RADIUS_CENTIMETERS,
					}
				),
				.userIndex = TARGET_USER_INDEX,
			}
		);

		fang::CollisionWorld world;
		CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
		world.Update(proxies);

		fang::MeleeSwingState swingState{};
		uint32_t              totalNewHitCount = 0;

		for (int frame = 0; frame < 40; ++frame)
		{
			fang::SweepHit              hits[fang::MAX_MELEE_SWING_HIT_COUNT];
			const fang::MeleeSwingInput input{
				.selfPosition      = position,
				.selfFacingRadians = 0.0f, // 攻撃側は常に +X (標的の方向)を向いている。
				.isAttackRequested = frame == 0,
				.selfUserIndex     = ATTACKER_USER_INDEX,
			};
			const fang::MeleeSwingResult result =
				fang::StepMeleeSwing(world, swingParameter, input, FRAME_SECONDS, &swingState, hits);

			totalNewHitCount += result.newHitCount;
		}

		CHECK(totalNewHitCount == 1);

		world.Shutdown();
	}
}


TEST_CASE("WolfDefeat: 振りの最中は位置も向きも変わらない")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::PursuitParameter pursuitParameter{};
	pursuitParameter.stopDistanceCentimeters = ENEMY_STOP_DISTANCE_CENTIMETERS;

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = true;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ ENEMY_REACH_CENTIMETERS, 0.0f, 0.0f };

	fang::EnPursuitState state = fang::EnPursuitState::Chase;

	// 雑魚の時間割(設計)と同じ。1周1.00秒のうち構え+判定+戻りで0.70秒 ➡ 42フレーム。
	fang::MeleeSwingParameter swingParameter{};
	swingParameter.windUpSeconds    = 0.30f;
	swingParameter.activeSeconds    = 0.15f;
	swingParameter.recoverySeconds  = 0.25f;
	swingParameter.cooldownSeconds  = 0.30f;
	swingParameter.reachCentimeters = ENEMY_REACH_CENTIMETERS;
	swingParameter.triggerMode      = fang::EnMeleeSwingTrigger::Continuous;

	fang::MeleeSwingState swingState{};
	fang::Vector3         position{};
	float                 facingRadians = 0.0f;

	bool sawSwingInProgress = false;

	for (int frame = 0; frame < 50; ++frame)
	{
		const fang::Vector3 positionBeforeThisFrame = position;
		const float         facingBeforeThisFrame   = facingRadians;

		// 3. 振り(移動より前。狼と同じ理由)。
		fang::SweepHit              hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingInput swingInput{
			.selfPosition      = position,
			.selfFacingRadians = facingRadians,
			.isAttackRequested = true,
			.selfUserIndex     = ATTACKER_USER_INDEX,
		};
		(void)fang::StepMeleeSwing(world, swingParameter, swingInput, FRAME_SECONDS, &swingState, hits);

		const bool isInProgress = fang::IsMeleeSwingInProgress(swingState);
		sawSwingInProgress      = sawSwingInProgress || isInProgress;

		// 4. 意思決定。振りの最中は答えを捨てる。
		fang::MoveIntent intent = fang::StepPursuit(pursuitParameter, blackboard, position, FRAME_SECONDS, &state);
		if (isInProgress)
		{
			intent = fang::MoveIntent{};
		}

		// 5. エフェクター。
		position += intent.desiredDelta;
		if (intent.wantsToTurn)
		{
			facingRadians = intent.facingRadians;
		}

		if (isInProgress)
		{
			CHECK(position.x == doctest::Approx(positionBeforeThisFrame.x));
			CHECK(position.z == doctest::Approx(positionBeforeThisFrame.z));
			CHECK(facingRadians == doctest::Approx(facingBeforeThisFrame));
		}
	}

	CHECK(sawSwingInProgress);

	world.Shutdown();
}


TEST_CASE("WolfDefeat: 当たるのは狼だけ")
{
	fang::MeleeSwingParameter parameter{};
	parameter.reachCentimeters = ENEMY_REACH_CENTIMETERS;

	// 牙の高さに合わせる ➡ 牙の球(y = fangHeightCentimeters)と確実に重なる。
	const fang::Vector3 targetPosition{ ENEMY_REACH_CENTIMETERS, parameter.fangHeightCentimeters, 0.0f };

	std::vector<fang::ColliderProxy> proxies;
	proxies.push_back(
		fang::ColliderProxy{
			.shape         = fang::MakeColliderShape(fang::Sphere{ .center = targetPosition, .radius = 5.0f }),
			.userIndex     = 1,
			.attributeMask = TEST_ATTRIBUTE_CHARACTER | TEST_ATTRIBUTE_ENEMY, // 雑魚どうし。
		}
	);
	proxies.push_back(
		fang::ColliderProxy{
			.shape         = fang::MakeColliderShape(fang::Sphere{ .center = targetPosition, .radius = 5.0f }),
			.userIndex     = 2,
			.attributeMask = TEST_ATTRIBUTE_PROP, // 置き物。
		}
	);
	proxies.push_back(
		fang::ColliderProxy{
			.shape         = fang::MakeColliderShape(fang::Sphere{ .center = targetPosition, .radius = 5.0f }),
			.userIndex     = TARGET_USER_INDEX,
			.attributeMask = TEST_ATTRIBUTE_CHARACTER | TEST_ATTRIBUTE_WOLF, // 狼。
		}
	);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));
	world.Update(proxies);

	fang::MeleeSwingState state{};
	uint32_t              totalNewHitCount = 0;
	uint32_t              hitWolfCount     = 0;

	for (int frame = 0; frame < 40; ++frame)
	{
		fang::SweepHit              hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingInput input{
			.selfPosition        = fang::Vector3{},
			.selfFacingRadians   = 0.0f,
			.isAttackRequested   = frame == 0,
			.selfUserIndex       = ATTACKER_USER_INDEX,
			.targetAttributeMask = TEST_ATTRIBUTE_WOLF,
		};
		const fang::MeleeSwingResult result =
			fang::StepMeleeSwing(world, parameter, input, FRAME_SECONDS, &state, hits);

		for (uint32_t hitIndex = 0; hitIndex < result.newHitCount; ++hitIndex)
		{
			if (hits[hitIndex].userIndex == TARGET_USER_INDEX)
			{
				++hitWolfCount;
			}
		}
		totalNewHitCount += result.newHitCount;
	}

	CHECK(totalNewHitCount == 1);
	CHECK(hitWolfCount == 1);

	world.Shutdown();
}
