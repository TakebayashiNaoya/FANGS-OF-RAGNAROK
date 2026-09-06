/**
 * @file EnemyDefeatBudgetTests.cpp
 * @brief 雑魚の撃破のヒープ確保と実機予算のテスト。32体を密集させて600フレーム振り続けてもヒープ確保が
 *        増えないこと、74登録+32体の感知・追跡+振りの掃引が実機予算の1割に収まる見込みを確かめる
 *        (EnemyEncounterBudgetTests と同じ流儀)。
 */
#include "AI/AI.h"
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/Budget.h"
#include "Scene/CharacterMovement.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <chrono>
#include <vector>


namespace
{
	constexpr uint32_t TEST_ATTRIBUTE_ENEMY = 1u << 2;

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


	/** @brief 感知・追跡・移動・当たりを Scene の振る舞いとしてまとめた、テスト専用の雑魚役。HP を持つ。 */
	class TestMinionBehavior final : public fang::IComponent
	{
	public:
		TestMinionBehavior(
			fang::CollisionWorld*         world,
			fang::GameObjectHandle        target,
			const fang::PerceptionParams& perceptionParams,
			const fang::PursuitParams&    pursuitParams,
			const fang::Vector3&          initialPosition
		)
			: m_world(world)
			, m_target(target)
			, m_perceptionParams(perceptionParams)
			, m_pursuitParams(pursuitParams)
			, m_position(initialPosition)
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
					.selfFacingRadians = 0.0f,
					.targetPosition    = targetPosition,
					.selfUserIndex     = self.index,
					.targetUserIndex   = m_target.index,
				};
				perception = fang::Sense(*m_world, m_perceptionParams, input);
			}

			fang::WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

			const fang::MoveIntent intent =
				fang::StepPursuit(m_pursuitParams, m_blackboard, m_position, deltaTimeSeconds, &m_state);

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

		fang::Vector3         m_position;
		fang::AgentBlackboard m_blackboard;
		fang::EnPursuitState  m_state = fang::EnPursuitState::Idle;
	};


	/** @brief 雑魚 1 体ぶんの当たり判定を登録する。攻撃の掃引が拾えるよう ENEMY 層を付ける。 */
	void RegisterMinionCollider(fang::Scene& scene, fang::GameObjectHandle handle)
	{
		constexpr float HALF_EXTENT = 20.0f;
		(void)scene.AddColliderComponent(
			handle,
			fang::ColliderComponent{
				.shapeType     = fang::EnShapeType::Sphere,
				.localBounds   = fang::Aabb{ .min = { -HALF_EXTENT, -HALF_EXTENT, -HALF_EXTENT },
											 .max = { HALF_EXTENT, HALF_EXTENT, HALF_EXTENT } },
				.isEnabled     = true,
				.attributeMask = TEST_ATTRIBUTE_ENEMY,
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


TEST_CASE("EnemyDefeat: 32体を密集させて600フレーム振り続けてもヒープ確保が増えない")
{
	CountingAllocator allocator;

	fang::Scene scene;
	CHECK(scene.Initialize(allocator, fang::SceneDesc{ .maxObjectCount = 40, .maxBehaviorCount = 40 }));

	const uint32_t allocationCountAfterInitialize = allocator.GetAllocationCount();

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::FrameAllocator frameAllocator;
	CHECK(frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"));

	const fang::GameObjectHandle target = scene.CreateObject();

	fang::SpawnScheduler scheduler;
	fang::SpawnParams    spawnParams{};
	spawnParams.intervalSeconds            = 0.05f; // テストを速く進めるため短くする(実際は 1 秒)。
	spawnParams.maximumAliveCount          = 32;
	spawnParams.minimumDistanceCentimeters = 50.0f; // 密集させるため、実際の湧きより近くに寄せる。
	spawnParams.maximumDistanceCentimeters = 200.0f;

	const fang::PerceptionParams perceptionParams{};
	const fang::PursuitParams    pursuitParams{};
	const fang::MeleeSwingParams swingParams{};

	constexpr uint32_t ATTACKER_USER_INDEX = 0xFFFFu;

	fang::MeleeSwingState swingState{};
	uint32_t              aliveCount = 0;

	for (int frame = 0; frame < 600; ++frame)
	{
		constexpr float deltaTimeSeconds = 1.0f / 60.0f;

		const fang::SpawnRequest request = scheduler.Update(deltaTimeSeconds, aliveCount, fang::Vector3{}, spawnParams);
		if (request.shouldSpawn)
		{
			const fang::GameObjectHandle handle = scene.CreateObject();
			if (handle.IsValid())
			{
				fang::IComponent* behavior = scene.AddBehavior<TestMinionBehavior>(
					handle,
					&world,
					target,
					perceptionParams,
					pursuitParams,
					request.position
				);
				if (behavior != nullptr)
				{
					(void)scene.AddHealthComponent(handle, fang::HealthComponent{});
					RegisterMinionCollider(scene, handle);
					++aliveCount;
				}
			}
		}

		// 攻撃側は原点に立ったまま振り続ける。Ready に戻るたびに押し直す(押しっぱなしでは 2 回目が始まらない)。
		const bool attackButtonDown = (swingState.phase == fang::EnMeleeSwingPhase::Ready);

		const fang::MeleeSwingInput swingInput{
			.selfPosition        = fang::Vector3{},
			.selfFacingRadians   = 0.0f,
			.isAttackRequested   = attackButtonDown,
			.selfUserIndex       = ATTACKER_USER_INDEX,
			.targetAttributeMask = TEST_ATTRIBUTE_ENEMY,
		};

		fang::SweepHit               hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		const fang::MeleeSwingResult swingResult =
			fang::StepMeleeSwing(world, swingParams, swingInput, deltaTimeSeconds, &swingState, hits);

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

			if (fang::ApplyDamage(health, swingParams.attackPower).wasDefeated)
			{
				scene.DestroyObject(victim);
			}
		}

		scene.Update(deltaTimeSeconds);

#if FANG_ENABLE_SCENE_VALIDATION
		// 32体の雑魚が毎周 Transform を書いても、書き手が重ならないこと（rigid-body-seams の完了条件）。
		CHECK(scene.GetDuplicateTransformWriteCount() == 0);
#endif

		frameAllocator.Reset();
		const std::span<const fang::ColliderProxy> colliderProxies = scene.BuildColliderProxies(frameAllocator);
		world.Update(colliderProxies);
	}

	CHECK(allocator.GetAllocationCount() == allocationCountAfterInitialize);

	frameAllocator.Shutdown();
	world.Shutdown();
	scene.Shutdown();
}


TEST_CASE("EnemyDefeat: 74登録+32体の感知・追跡+振りの掃引が実機予算の1割に収まる見込み")
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
				.userIndex     = MINION_USER_INDEX_BASE + index,
				.attributeMask = TEST_ATTRIBUTE_ENEMY,
			}
		);
	}

	world.Update(proxies);
	CHECK(world.GetColliderCount() == PROP_COUNT + WOLF_COUNT + MINION_COUNT);

	const fang::PerceptionParams perceptionParams{};
	const fang::PursuitParams    pursuitParams{};
	const fang::MeleeSwingParams swingParams{};
	const fang::Vector3          targetPosition{ 0.0f, 0.0f, 500.0f };

	std::vector<fang::AgentBlackboard> blackboards(MINION_COUNT);
	std::vector<fang::EnPursuitState>  states(MINION_COUNT, fang::EnPursuitState::Chase);
	for (uint32_t index = 0; index < MINION_COUNT; ++index)
	{
		blackboards[index].isTargetVisible        = true;
		blackboards[index].hasLastSeenPosition    = true;
		blackboards[index].lastSeenTargetPosition = targetPosition;
	}

	fang::MeleeSwingState swingState{};
	swingState.phase = fang::EnMeleeSwingPhase::Active; // 掃引そのものの費用を測るため、判定区間から始める。

	const float measuredSeconds = MeasureSeconds([&]() {
		for (uint32_t index = 0; index < MINION_COUNT; ++index)
		{
			const fang::PerceptionInput input{
				.selfPosition      = minionPositions[index],
				.selfFacingRadians = 0.0f,
				.targetPosition    = targetPosition,
				.selfUserIndex     = MINION_USER_INDEX_BASE + index,
				.targetUserIndex   = WOLF_USER_INDEX_BASE,
			};
			const fang::PerceptionResult result = fang::Sense(world, perceptionParams, input);
			fang::WritePerception(result, targetPosition, 1.0f / 60.0f, &blackboards[index]);

			(void)fang::StepPursuit(
				pursuitParams,
				blackboards[index],
				minionPositions[index],
				1.0f / 60.0f,
				&states[index]
			);
		}

		const fang::MeleeSwingInput swingInput{
			.selfPosition        = targetPosition,
			.selfFacingRadians   = 0.0f,
			.isAttackRequested   = true,
			.selfUserIndex       = WOLF_USER_INDEX_BASE,
			.targetAttributeMask = TEST_ATTRIBUTE_ENEMY,
		};
		fang::SweepHit hits[fang::MAX_MELEE_SWING_HIT_COUNT];
		(void)fang::StepMeleeSwing(world, swingParams, swingInput, 1.0f / 60.0f, &swingState, hits);
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
