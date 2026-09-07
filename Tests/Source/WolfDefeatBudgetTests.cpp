/**
 * @file WolfDefeatBudgetTests.cpp
 * @brief 狼の被弾のヒープ確保と実機予算のテスト。32体の雑魚が狼を囲んで900フレーム振り続けても
 *        ヒープ確保が増えず、狼が撃破された後も落ちないこと、視線32本+牙33本(雑魚32+狼1)の掃引が
 *        実機予算の1割に収まる見込みを確かめる(EnemyDefeatBudgetTests と同じ流儀)。
 */
#include "AI/AI.h"
#include "Collision/Collision.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/Budget.h"
#include "Scene/CharacterController.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <chrono>
#include <cmath>
#include <vector>


namespace
{
	constexpr uint32_t TEST_ATTRIBUTE_CHARACTER = 1u << 0;
	constexpr uint32_t TEST_ATTRIBUTE_PROP      = 1u << 1;
	constexpr uint32_t TEST_ATTRIBUTE_WOLF      = 1u << 3;

	// EnemyParameter(設計)と同じ値。
	constexpr float ENEMY_STOP_DISTANCE_CENTIMETERS = 120.0f;
	constexpr float ENEMY_REACH_CENTIMETERS         = 150.0f;

	/** @brief 呼ばれた回数を数えるだけのアロケータ。更新のたびのヒープ確保が 0 であることの確認に使う。 */
	class CountingAllocator final : public fang::IAllocator
	{
	public:
		[[nodiscard]] const char* GetName() const override { return "Counting"; }

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


	/** @brief 感知・追跡・振り・被弾判定を Scene の振る舞いとしてまとめた、テスト専用の雑魚役。 */
	class TestEnemyController final : public fang::IComponent
	{
	public:
		TestEnemyController(
			fang::CollisionWorld*            world,
			fang::ActorHandle                target,
			const fang::PerceptionParameter& perceptionParameter,
			const fang::PursuitParameter&    pursuitParameter,
			const fang::MeleeSwingParameter& swingParameter,
			const fang::Vector3&             initialPosition,
			float                            initialFacingRadians
		)
			: m_world(world)
			, m_target(target)
			, m_perceptionParameter(perceptionParameter)
			, m_pursuitParameter(pursuitParameter)
			, m_swingParameter(swingParameter)
			, m_position(initialPosition)
			, m_facingRadians(initialFacingRadians)
		{
		}

		void Update(float deltaTimeSeconds, fang::Actor self) override
		{
			const fang::Actor target = self.GetActorFromHandle(m_target);
			fang::Vector3     targetPosition;
			const bool        hasTarget = target.IsValid();
			if (hasTarget)
			{
				targetPosition = target.GetWorldPosition();
			}

			fang::PerceptionResult perception;
			if (hasTarget && m_world != nullptr)
			{
				const fang::PerceptionInput input{
					.selfPosition      = m_position,
					.selfFacingRadians = m_facingRadians,
					.targetPosition    = targetPosition,
					.selfUserIndex     = self.GetIndex(),
					.targetUserIndex   = target.GetIndex(),
				};
				perception = fang::Sense(*m_world, m_perceptionParameter, input);
			}

			fang::WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

			if (m_world != nullptr)
			{
				const bool isAttackRequested =
					m_blackboard.isTargetVisible &&
					m_blackboard.distanceToTargetCentimeters <= m_swingParameter.reachCentimeters;

				const fang::MeleeSwingInput swingInput{
					.selfPosition        = m_position,
					.selfFacingRadians   = m_facingRadians,
					.isAttackRequested   = isAttackRequested,
					.selfUserIndex       = self.GetIndex(),
					.targetAttributeMask = TEST_ATTRIBUTE_WOLF,
				};

				fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
				const fang::MeleeSwingResult swingResult =
					fang::StepMeleeSwing(*m_world, m_swingParameter, swingInput, deltaTimeSeconds, &m_swingState, hits);

				for (uint32_t hitIndex = 0; hitIndex < swingResult.newHitCount; ++hitIndex)
				{
					fang::Actor victim = self.GetActorFromIndex(hits[hitIndex].userIndex);
					if (!victim.IsValid() || victim.IsPendingDestroy())
					{
						continue;
					}

					fang::HealthComponent* health = victim.GetHealthComponent();
					if (health == nullptr)
					{
						continue;
					}

					if (fang::ApplyDamage(health, m_swingParameter.attackPower).wasDefeated)
					{
						victim.Destroy();
					}
				}
			}

			fang::MoveIntent intent =
				fang::StepPursuit(m_pursuitParameter, m_blackboard, m_position, deltaTimeSeconds, &m_state);
			if (fang::IsMeleeSwingInProgress(m_swingState))
			{
				intent = fang::MoveIntent{};
			}

			const std::span<const fang::Contact> contacts =
				(m_world != nullptr) ? m_world->GetContacts() : std::span<const fang::Contact>{};

			const fang::ContactMoveResult moveResult =
				fang::MoveWithContacts(m_position, intent.desiredDelta, contacts, self.GetIndex());
			m_position = moveResult.position;

			(void)self.SetTransform(m_position, 0.0f);
		}


	private:
		fang::CollisionWorld*     m_world = nullptr;
		fang::ActorHandle         m_target;
		fang::PerceptionParameter m_perceptionParameter;
		fang::PursuitParameter    m_pursuitParameter;
		fang::MeleeSwingParameter m_swingParameter;

		fang::Vector3         m_position;
		float                 m_facingRadians = 0.0f;
		fang::AgentBlackboard m_blackboard;
		fang::EnPursuitState  m_state = fang::EnPursuitState::Idle;
		fang::MeleeSwingState m_swingState;
	};


	/** @brief 雑魚 1 体ぶんの当たり判定を登録する。置き物としては数えない(遮蔽に使わない)層。 */
	void RegisterEnemyCollider(fang::Scene& scene, fang::ActorHandle handle)
	{
		constexpr float HALF_EXTENT = 20.0f;
		(void)scene.AddColliderComponent(
			handle,
			fang::ColliderComponent{
				.shapeType     = fang::EnShapeType::Sphere,
				.localBounds   = fang::Aabb{ .min = { -HALF_EXTENT, -HALF_EXTENT, -HALF_EXTENT },
											 .max = { HALF_EXTENT, HALF_EXTENT, HALF_EXTENT } },
				.isEnabled     = true,
				.attributeMask = TEST_ATTRIBUTE_CHARACTER,
			}
		);
	}


	/** @brief 関数を呼ぶのにかかった秒を測る。 */
	template <typename Function> [[nodiscard]] float MeasureSeconds(Function&& function)
	{
		const auto start = std::chrono::steady_clock::now();
		function();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}
} // namespace


TEST_CASE("WolfDefeat: 32体が狼を囲んで900フレーム振り続けてもヒープ確保が増えず、撃破後も落ちない")
{
	CountingAllocator allocator;

	fang::Scene scene;
	CHECK(scene.Initialize(allocator, fang::SceneDesc{ .maxObjectCount = 40, .maxBehaviorCount = 40 }));

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::FrameAllocator frameAllocator;
	CHECK(frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"));

	// 狼役。設計の調整値と同じ HP・無敵時間。原点で静止したまま。
	const fang::ActorHandle wolf = scene.CreateObject();
	(void)scene.AddHealthComponent(
		wolf,
		fang::HealthComponent{ .maximumHitPoints = 300.0f, .currentHitPoints = 300.0f, .invincibleSeconds = 0.5f }
	);
	(void)scene.AddColliderComponent(
		wolf,
		fang::ColliderComponent{
			.shapeType     = fang::EnShapeType::Sphere,
			.localBounds   = fang::Aabb{ .min = { -40.0f, -40.0f, -40.0f }, .max = { 40.0f, 40.0f, 40.0f } },
			.isEnabled     = true,
			.attributeMask = TEST_ATTRIBUTE_CHARACTER | TEST_ATTRIBUTE_WOLF,
		}
	);

	const fang::PerceptionParameter perceptionParameter{ .blockerAttributeMask = TEST_ATTRIBUTE_PROP };
	const fang::PursuitParameter    pursuitParameter{ .stopDistanceCentimeters = ENEMY_STOP_DISTANCE_CENTIMETERS };

	fang::MeleeSwingParameter swingParameter{};
	swingParameter.windUpSeconds    = 0.30f;
	swingParameter.activeSeconds    = 0.15f;
	swingParameter.recoverySeconds  = 0.25f;
	swingParameter.cooldownSeconds  = 0.30f;
	swingParameter.reachCentimeters = ENEMY_REACH_CENTIMETERS;
	swingParameter.attackPower      = 25.0f;
	swingParameter.triggerMode      = fang::EnMeleeSwingTrigger::Continuous;

	// 32体を +X 軸上に距離をずらして並べる(150〜615cm)。狼へ向く一定の向き(-X、PI)で見えるように
	// 揃え、距離をずらすことで間合いへ入るタイミングをばらけさせる(全員が同時に振ると、無敵時間より
	// 振りの周期のほうが利いてしまい、狼が実測どおりの速さで倒れない)。
	constexpr uint32_t ENEMY_COUNT                   = 32;
	constexpr float    ENEMY_FACING_RADIANS          = fang::PI; // -X 向き。狼(原点)はその方向に居る。
	constexpr float    SPAWN_RADIUS_STEP_CENTIMETERS = 15.0f;

	std::vector<fang::ActorHandle> enemyHandles;
	enemyHandles.reserve(ENEMY_COUNT);

	for (uint32_t index = 0; index < ENEMY_COUNT; ++index)
	{
		const fang::Vector3 position{
			ENEMY_STOP_DISTANCE_CENTIMETERS + 30.0f + static_cast<float>(index) * SPAWN_RADIUS_STEP_CENTIMETERS,
			0.0f,
			0.0f,
		};

		const fang::ActorHandle handle = scene.CreateObject();
		CHECK(handle.IsValid());

		fang::IComponent* behavior = scene.AddBehavior<TestEnemyController>(
			handle,
			&world,
			wolf,
			perceptionParameter,
			pursuitParameter,
			swingParameter,
			position,
			ENEMY_FACING_RADIANS
		);
		CHECK(behavior != nullptr);

		RegisterEnemyCollider(scene, handle);
		enemyHandles.push_back(handle);
	}

	const uint32_t allocationCountAfterSetup = allocator.GetAllocationCount();

	for (int frame = 0; frame < 900; ++frame)
	{
		constexpr float deltaTimeSeconds = 1.0f / 60.0f;

		scene.Update(deltaTimeSeconds);

#if FANG_ENABLE_SCENE_VALIDATION
		// 狼・32体の雑魚が毎周 Transform を書いても、書き手が重ならないこと（rigid-body-seams の完了条件）。
		CHECK(scene.GetDuplicateTransformWriteCount() == 0);
#endif

		frameAllocator.Reset();
		const std::span<const fang::ColliderProxy> colliderProxies = scene.BuildColliderProxies(frameAllocator);
		world.Update(colliderProxies);
	}

	// 32体×攻撃力25、無敵0.5秒 ➡ 最短6秒(360フレーム)で倒れる計算(設計)。
	// 押し戻しを水平面だけで解くようにした後は、密集した雑魚どうしの押し合いがほどけるのに
	// 数フレームかかるため実測711フレームで撃破される(ADR-061、狼どうしの接触で地面にめり込む設計.md)。
	// 900フレームなら撃破される見込み。
	CHECK_FALSE(scene.IsValid(wolf));

	// 生きているオブジェクトは雑魚(最大32)だけ。狼が消えた分だけ減っている。
	CHECK(scene.GetActiveObjectCount() <= ENEMY_COUNT);

	CHECK(allocator.GetAllocationCount() == allocationCountAfterSetup);

	frameAllocator.Shutdown();
	world.Shutdown();
	scene.Shutdown();
}


TEST_CASE("WolfDefeat: 視線32本+牙33本(雑魚32+狼1)の掃引が実機予算の1割に収まる見込み")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t PROP_COUNT  = 40;
	constexpr uint32_t WOLF_COUNT  = 2;
	constexpr uint32_t ENEMY_COUNT = 32;

	constexpr uint32_t WOLF_USER_INDEX_BASE  = PROP_COUNT;
	constexpr uint32_t ENEMY_USER_INDEX_BASE = PROP_COUNT + WOLF_COUNT;

	std::vector<fang::ColliderProxy> proxies;

	for (uint32_t index = 0; index < PROP_COUNT; ++index)
	{
		const fang::Vector3 center{
			static_cast<float>(index % 8) * 100.0f,
			0.0f,
			static_cast<float>(index / 8) * 100.0f,
		};
		proxies.push_back(
			fang::ColliderProxy{
				.shape = fang::MakeColliderShape(fang::OBB{ .center = center, .halfExtents = { 40.0f, 40.0f, 40.0f } }),
				.userIndex     = index,
				.attributeMask = TEST_ATTRIBUTE_PROP,
			}
		);
	}

	for (uint32_t index = 0; index < WOLF_COUNT; ++index)
	{
		const fang::Vector3 center{ static_cast<float>(index) * 100.0f, 0.0f, 500.0f };
		proxies.push_back(
			fang::ColliderProxy{
				.shape = fang::MakeColliderShape(
					fang::Capsule{ .pointA = center,
								   .pointB = center + fang::Vector3{ 0.0f, 100.0f, 0.0f },
								   .radius = 20.0f }
				),
				.userIndex     = WOLF_USER_INDEX_BASE + index,
				.attributeMask = TEST_ATTRIBUTE_CHARACTER | TEST_ATTRIBUTE_WOLF,
			}
		);
	}

	std::vector<fang::Vector3> enemyPositions(ENEMY_COUNT);
	for (uint32_t index = 0; index < ENEMY_COUNT; ++index)
	{
		enemyPositions[index] = fang::Vector3{ static_cast<float>(index) * 20.0f - 320.0f, 0.0f, 700.0f };

		proxies.push_back(
			fang::ColliderProxy{
				.shape = fang::MakeColliderShape(
					fang::Capsule{
						.pointA = enemyPositions[index],
						.pointB = enemyPositions[index] + fang::Vector3{ 0.0f, 100.0f, 0.0f },
						.radius = 20.0f,
					}
				),
				.userIndex     = ENEMY_USER_INDEX_BASE + index,
				.attributeMask = TEST_ATTRIBUTE_CHARACTER,
			}
		);
	}

	world.Update(proxies);
	CHECK(world.GetColliderCount() == PROP_COUNT + WOLF_COUNT + ENEMY_COUNT);

	const fang::PerceptionParameter perceptionParameter{ .blockerAttributeMask = TEST_ATTRIBUTE_PROP };
	const fang::PursuitParameter    pursuitParameter{};
	const fang::MeleeSwingParameter enemySwingParameter{ .reachCentimeters = ENEMY_REACH_CENTIMETERS };
	const fang::MeleeSwingParameter wolfSwingParameter{};
	const fang::Vector3             wolfPosition{ 0.0f, 0.0f, 500.0f };

	std::vector<fang::AgentBlackboard> blackboards(ENEMY_COUNT);
	std::vector<fang::EnPursuitState>  states(ENEMY_COUNT, fang::EnPursuitState::Chase);
	std::vector<fang::MeleeSwingState> enemySwingStates(ENEMY_COUNT);
	for (uint32_t index = 0; index < ENEMY_COUNT; ++index)
	{
		blackboards[index].isTargetVisible        = true;
		blackboards[index].hasLastSeenPosition    = true;
		blackboards[index].lastSeenTargetPosition = wolfPosition;

		// 掃引そのものの費用を測るため、判定区間から始める。
		enemySwingStates[index].phase = fang::EnMeleeSwingPhase::Active;
	}

	fang::MeleeSwingState wolfSwingState{};
	wolfSwingState.phase = fang::EnMeleeSwingPhase::Active;

	const float measuredSeconds = MeasureSeconds([&]() {
		for (uint32_t index = 0; index < ENEMY_COUNT; ++index)
		{
			const fang::PerceptionInput input{
				.selfPosition      = enemyPositions[index],
				.selfFacingRadians = 0.0f,
				.targetPosition    = wolfPosition,
				.selfUserIndex     = ENEMY_USER_INDEX_BASE + index,
				.targetUserIndex   = WOLF_USER_INDEX_BASE,
			};
			const fang::PerceptionResult result = fang::Sense(world, perceptionParameter, input);
			fang::WritePerception(result, wolfPosition, 1.0f / 60.0f, &blackboards[index]);

			(void)fang::StepPursuit(
				pursuitParameter,
				blackboards[index],
				enemyPositions[index],
				1.0f / 60.0f,
				&states[index]
			);

			const fang::MeleeSwingInput enemySwingInput{
				.selfPosition        = enemyPositions[index],
				.selfFacingRadians   = 0.0f,
				.isAttackRequested   = true,
				.selfUserIndex       = ENEMY_USER_INDEX_BASE + index,
				.targetAttributeMask = TEST_ATTRIBUTE_WOLF,
			};
			fang::SweepHit enemyHits[fang::MAX_MELEE_SWING_HIT_COUNT];
			(void)fang::StepMeleeSwing(
				world,
				enemySwingParameter,
				enemySwingInput,
				1.0f / 60.0f,
				&enemySwingStates[index],
				enemyHits
			);
		}

		const fang::MeleeSwingInput wolfSwingInput{
			.selfPosition        = wolfPosition,
			.selfFacingRadians   = 0.0f,
			.isAttackRequested   = true,
			.selfUserIndex       = WOLF_USER_INDEX_BASE,
			.targetAttributeMask = TEST_ATTRIBUTE_CHARACTER,
		};
		fang::SweepHit wolfHits[fang::MAX_MELEE_SWING_HIT_COUNT];
		(void)fang::StepMeleeSwing(world, wolfSwingParameter, wolfSwingInput, 1.0f / 60.0f, &wolfSwingState, wolfHits);
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
