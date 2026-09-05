/**
 * @file WolfDefeatBudgetTests.cpp
 * @brief 狼の被弾のヒープ確保と実機予算のテスト。32体の雑魚が狼を囲んで600フレーム振り続けても
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
#include "Scene/CharacterMovement.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <chrono>
#include <cmath>
#include <vector>


namespace
{
	constexpr uint32_t TEST_LAYER_CHARACTER = 1u << 0;
	constexpr uint32_t TEST_LAYER_PROP      = 1u << 1;
	constexpr uint32_t TEST_LAYER_WOLF      = 1u << 3;

	// MinionParams(設計)と同じ値。
	constexpr float MINION_STOP_DISTANCE_CENTIMETERS = 120.0f;
	constexpr float MINION_REACH_CENTIMETERS         = 150.0f;

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
	class TestMinionBehavior final : public fang::IComponent
	{
	public:
		TestMinionBehavior(
			fang::CollisionWorld*         world,
			fang::GameObjectHandle        target,
			const fang::PerceptionParams& perceptionParams,
			const fang::PursuitParams&    pursuitParams,
			const fang::MeleeSwingParams& swingParams,
			const fang::Vector3&          initialPosition,
			float                         initialFacingRadians
		)
			: m_world(world)
			, m_target(target)
			, m_perceptionParams(perceptionParams)
			, m_pursuitParams(pursuitParams)
			, m_swingParams(swingParams)
			, m_position(initialPosition)
			, m_facingRadians(initialFacingRadians)
		{
		}

		void Update(float deltaTimeSeconds, fang::GameObjectHandle self, fang::Scene& scene) override
		{
			fang::Vector3 targetPosition;
			const bool    hasTarget = scene.IsValid(m_target);
			if (hasTarget)
			{
				const fang::Matrix4x4 targetWorld = scene.GetWorldMatrix(m_target);
				targetPosition = fang::Vector3{ targetWorld.m[3][0], targetWorld.m[3][1], targetWorld.m[3][2] };
			}

			fang::PerceptionResult perception;
			if (hasTarget && m_world != nullptr)
			{
				const fang::PerceptionInput input{
					.selfPosition      = m_position,
					.selfFacingRadians = m_facingRadians,
					.targetPosition    = targetPosition,
					.selfUserIndex     = self.index,
					.targetUserIndex   = m_target.index,
				};
				perception = fang::Sense(*m_world, m_perceptionParams, input);
			}

			fang::WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

			if (m_world != nullptr)
			{
				const bool isAttackRequested =
					m_blackboard.isTargetVisible &&
					m_blackboard.distanceToTargetCentimeters <= m_swingParams.reachCentimeters;

				const fang::MeleeSwingInput swingInput{
					.selfPosition      = m_position,
					.selfFacingRadians = m_facingRadians,
					.isAttackRequested = isAttackRequested,
					.selfUserIndex     = self.index,
					.targetLayerMask   = TEST_LAYER_WOLF,
				};

				fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
				const fang::MeleeSwingResult swingResult =
					fang::StepMeleeSwing(*m_world, m_swingParams, swingInput, deltaTimeSeconds, &m_swingState, hits);

				for (uint32_t hitIndex = 0; hitIndex < swingResult.newHitCount; ++hitIndex)
				{
					const fang::GameObjectHandle victim = scene.GetHandleFromIndex(hits[hitIndex].userIndex);
					if (!victim.IsValid() || scene.IsPendingDestroy(victim))
					{
						continue;
					}

					fang::HealthComponent* health = scene.GetHealthComponent(victim);
					if (health == nullptr)
					{
						continue;
					}

					if (fang::ApplyDamage(health, m_swingParams.attackPower).wasDefeated)
					{
						scene.DestroyObject(victim);
					}
				}
			}

			fang::MoveIntent intent =
				fang::StepPursuit(m_pursuitParams, m_blackboard, m_position, deltaTimeSeconds, &m_state);
			if (fang::IsMeleeSwingInProgress(m_swingState))
			{
				intent = fang::MoveIntent{};
			}

			const std::span<const fang::Contact> contacts =
				(m_world != nullptr) ? m_world->GetContacts() : std::span<const fang::Contact>{};

			const fang::ContactMoveResult moveResult =
				fang::MoveWithContacts(m_position, intent.desiredDelta, contacts, self.index);
			m_position = moveResult.position;

			(void)scene.SetLocalTransform(self, m_position, 0.0f);
		}


	private:
		fang::CollisionWorld*  m_world = nullptr;
		fang::GameObjectHandle m_target;
		fang::PerceptionParams m_perceptionParams;
		fang::PursuitParams    m_pursuitParams;
		fang::MeleeSwingParams m_swingParams;

		fang::Vector3         m_position;
		float                 m_facingRadians = 0.0f;
		fang::AgentBlackboard m_blackboard;
		fang::EnPursuitState  m_state = fang::EnPursuitState::Idle;
		fang::MeleeSwingState m_swingState;
	};


	/** @brief 雑魚 1 体ぶんの当たり判定を登録する。置き物としては数えない(遮蔽に使わない)層。 */
	void RegisterMinionCollider(fang::Scene& scene, fang::GameObjectHandle handle)
	{
		constexpr float HALF_EXTENT = 20.0f;
		(void)scene.AddColliderComponent(
			handle,
			fang::ColliderComponent{
				.shapeType   = fang::EnShapeType::Sphere,
				.localBounds = fang::Aabb{ .min = { -HALF_EXTENT, -HALF_EXTENT, -HALF_EXTENT },
										   .max = { HALF_EXTENT, HALF_EXTENT, HALF_EXTENT } },
				.isEnabled   = true,
				.layerMask   = TEST_LAYER_CHARACTER,
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


TEST_CASE("WolfDefeat: 32体が狼を囲んで600フレーム振り続けてもヒープ確保が増えず、撃破後も落ちない")
{
	CountingAllocator allocator;

	fang::Scene scene;
	CHECK(scene.Initialize(allocator, fang::SceneDesc{ .maxObjectCount = 40, .maxBehaviorCount = 40 }));

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::FrameAllocator frameAllocator;
	CHECK(frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"));

	// 狼役。設計の調整値と同じ HP・無敵時間。原点で静止したまま。
	const fang::GameObjectHandle wolf = scene.CreateObject();
	(void)scene.AddHealthComponent(
		wolf,
		fang::HealthComponent{ .maximumHitPoints = 300.0f, .currentHitPoints = 300.0f, .invincibleSeconds = 0.5f }
	);
	(void)scene.AddColliderComponent(
		wolf,
		fang::ColliderComponent{
			.shapeType   = fang::EnShapeType::Sphere,
			.localBounds = fang::Aabb{ .min = { -40.0f, -40.0f, -40.0f }, .max = { 40.0f, 40.0f, 40.0f } },
			.isEnabled   = true,
			.layerMask   = TEST_LAYER_CHARACTER | TEST_LAYER_WOLF,
		}
	);

	const fang::PerceptionParams perceptionParams{ .blockerLayerMask = TEST_LAYER_PROP };
	const fang::PursuitParams    pursuitParams{ .stopDistanceCentimeters = MINION_STOP_DISTANCE_CENTIMETERS };

	fang::MeleeSwingParams swingParams{};
	swingParams.windUpSeconds    = 0.30f;
	swingParams.activeSeconds    = 0.15f;
	swingParams.recoverySeconds  = 0.25f;
	swingParams.cooldownSeconds  = 0.30f;
	swingParams.reachCentimeters = MINION_REACH_CENTIMETERS;
	swingParams.attackPower      = 25.0f;
	swingParams.triggerMode      = fang::EnMeleeSwingTrigger::Continuous;

	// 32体を +X 軸上に距離をずらして並べる(150〜615cm)。狼へ向く一定の向き(-X、PI)で見えるように
	// 揃え、距離をずらすことで間合いへ入るタイミングをばらけさせる(全員が同時に振ると、無敵時間より
	// 振りの周期のほうが利いてしまい、狼が実測どおりの速さで倒れない)。
	constexpr uint32_t MINION_COUNT                  = 32;
	constexpr float    MINION_FACING_RADIANS         = fang::PI; // -X 向き。狼(原点)はその方向に居る。
	constexpr float    SPAWN_RADIUS_STEP_CENTIMETERS = 15.0f;

	std::vector<fang::GameObjectHandle> minionHandles;
	minionHandles.reserve(MINION_COUNT);

	for (uint32_t index = 0; index < MINION_COUNT; ++index)
	{
		const fang::Vector3 position{
			MINION_STOP_DISTANCE_CENTIMETERS + 30.0f + static_cast<float>(index) * SPAWN_RADIUS_STEP_CENTIMETERS,
			0.0f,
			0.0f,
		};

		const fang::GameObjectHandle handle = scene.CreateObject();
		CHECK(handle.IsValid());

		fang::IComponent* behavior = scene.AddBehavior<TestMinionBehavior>(
			handle,
			&world,
			wolf,
			perceptionParams,
			pursuitParams,
			swingParams,
			position,
			MINION_FACING_RADIANS
		);
		CHECK(behavior != nullptr);

		RegisterMinionCollider(scene, handle);
		minionHandles.push_back(handle);
	}

	const uint32_t allocationCountAfterSetup = allocator.GetAllocationCount();

	for (int frame = 0; frame < 600; ++frame)
	{
		constexpr float deltaTimeSeconds = 1.0f / 60.0f;

		scene.Update(deltaTimeSeconds);

		frameAllocator.Reset();
		const std::span<const fang::ColliderProxy> colliderProxies = scene.BuildColliderProxies(frameAllocator);
		world.Update(colliderProxies);
	}

	// 32体×攻撃力25、無敵0.5秒 ➡ 最短6秒(360フレーム)で倒れる計算(設計)。600フレームなら撃破される見込み。
	CHECK_FALSE(scene.IsValid(wolf));

	// 生きているオブジェクトは雑魚(最大32)だけ。狼が消えた分だけ減っている。
	CHECK(scene.GetActiveObjectCount() <= MINION_COUNT);

	CHECK(allocator.GetAllocationCount() == allocationCountAfterSetup);

	frameAllocator.Shutdown();
	world.Shutdown();
	scene.Shutdown();
}


TEST_CASE("WolfDefeat: 視線32本+牙33本(雑魚32+狼1)の掃引が実機予算の1割に収まる見込み")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t PROP_COUNT   = 40;
	constexpr uint32_t WOLF_COUNT   = 2;
	constexpr uint32_t MINION_COUNT = 32;

	constexpr uint32_t WOLF_USER_INDEX_BASE   = PROP_COUNT;
	constexpr uint32_t MINION_USER_INDEX_BASE = PROP_COUNT + WOLF_COUNT;

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
				.userIndex = index,
				.layerMask = TEST_LAYER_PROP,
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
				.userIndex = WOLF_USER_INDEX_BASE + index,
				.layerMask = TEST_LAYER_CHARACTER | TEST_LAYER_WOLF,
			}
		);
	}

	std::vector<fang::Vector3> minionPositions(MINION_COUNT);
	for (uint32_t index = 0; index < MINION_COUNT; ++index)
	{
		minionPositions[index] = fang::Vector3{ static_cast<float>(index) * 20.0f - 320.0f, 0.0f, 700.0f };

		proxies.push_back(
			fang::ColliderProxy{
				.shape = fang::MakeColliderShape(
					fang::Capsule{
						.pointA = minionPositions[index],
						.pointB = minionPositions[index] + fang::Vector3{ 0.0f, 100.0f, 0.0f },
						.radius = 20.0f,
					}
				),
				.userIndex = MINION_USER_INDEX_BASE + index,
				.layerMask = TEST_LAYER_CHARACTER,
			}
		);
	}

	world.Update(proxies);
	CHECK(world.GetColliderCount() == PROP_COUNT + WOLF_COUNT + MINION_COUNT);

	const fang::PerceptionParams perceptionParams{ .blockerLayerMask = TEST_LAYER_PROP };
	const fang::PursuitParams    pursuitParams{};
	const fang::MeleeSwingParams minionSwingParams{ .reachCentimeters = MINION_REACH_CENTIMETERS };
	const fang::MeleeSwingParams wolfSwingParams{};
	const fang::Vector3          wolfPosition{ 0.0f, 0.0f, 500.0f };

	std::vector<fang::AgentBlackboard> blackboards(MINION_COUNT);
	std::vector<fang::EnPursuitState>  states(MINION_COUNT, fang::EnPursuitState::Chase);
	std::vector<fang::MeleeSwingState> minionSwingStates(MINION_COUNT);
	for (uint32_t index = 0; index < MINION_COUNT; ++index)
	{
		blackboards[index].isTargetVisible        = true;
		blackboards[index].hasLastSeenPosition    = true;
		blackboards[index].lastSeenTargetPosition = wolfPosition;

		// 掃引そのものの費用を測るため、判定区間から始める。
		minionSwingStates[index].phase = fang::EnMeleeSwingPhase::Active;
	}

	fang::MeleeSwingState wolfSwingState{};
	wolfSwingState.phase = fang::EnMeleeSwingPhase::Active;

	const float measuredSeconds = MeasureSeconds([&]() {
		for (uint32_t index = 0; index < MINION_COUNT; ++index)
		{
			const fang::PerceptionInput input{
				.selfPosition      = minionPositions[index],
				.selfFacingRadians = 0.0f,
				.targetPosition    = wolfPosition,
				.selfUserIndex     = MINION_USER_INDEX_BASE + index,
				.targetUserIndex   = WOLF_USER_INDEX_BASE,
			};
			const fang::PerceptionResult result = fang::Sense(world, perceptionParams, input);
			fang::WritePerception(result, wolfPosition, 1.0f / 60.0f, &blackboards[index]);

			(void)fang::StepPursuit(
				pursuitParams,
				blackboards[index],
				minionPositions[index],
				1.0f / 60.0f,
				&states[index]
			);

			const fang::MeleeSwingInput minionSwingInput{
				.selfPosition      = minionPositions[index],
				.selfFacingRadians = 0.0f,
				.isAttackRequested = true,
				.selfUserIndex     = MINION_USER_INDEX_BASE + index,
				.targetLayerMask   = TEST_LAYER_WOLF,
			};
			fang::SweepHit minionHits[fang::MAX_MELEE_SWING_HIT_COUNT];
			(void)fang::StepMeleeSwing(
				world,
				minionSwingParams,
				minionSwingInput,
				1.0f / 60.0f,
				&minionSwingStates[index],
				minionHits
			);
		}

		const fang::MeleeSwingInput wolfSwingInput{
			.selfPosition      = wolfPosition,
			.selfFacingRadians = 0.0f,
			.isAttackRequested = true,
			.selfUserIndex     = WOLF_USER_INDEX_BASE,
			.targetLayerMask   = TEST_LAYER_CHARACTER,
		};
		fang::SweepHit wolfHits[fang::MAX_MELEE_SWING_HIT_COUNT];
		(void)fang::StepMeleeSwing(world, wolfSwingParams, wolfSwingInput, 1.0f / 60.0f, &wolfSwingState, wolfHits);
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
