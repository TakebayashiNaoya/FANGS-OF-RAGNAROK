/**
 * @file EnemyEncounterBudgetTests.cpp
 * @brief 雑魚との遭遇のヒープ確保と実機予算のテスト。32 体を湧かせて長時間回してもヒープ確保が増えないこと、
 *        74 登録 + 32 体の感知・追跡が実機予算の 1 割に収まる見込みを確かめる(CollisionBudgetTests と同じ流儀)。
 */
#include "AI/AI.h"
#include "Collision/Collision.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/Budget.h"
#include "Scene/CharacterController.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <chrono>
#include <vector>


namespace
{
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


	/** @brief センサー・ブラックボード・意思決定・移動を Scene の振る舞いとしてまとめた、テスト専用の雑魚役。 */
	class TestEnemyController final : public fang::IComponent
	{
	public:
		TestEnemyController(
			fang::CollisionWorld*            world,
			fang::ActorHandle                target,
			const fang::PerceptionParameter& perceptionParameter,
			const fang::PursuitParameter&    pursuitParameter,
			const fang::Vector3&             initialPosition
		)
			: m_world(world)
			, m_target(target)
			, m_perceptionParameter(perceptionParameter)
			, m_pursuitParameter(pursuitParameter)
			, m_position(initialPosition)
		{
		}

		void Update(float deltaTimeSeconds, fang::ActorHandle self, fang::Scene& scene) override
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
				perception = fang::Sense(*m_world, m_perceptionParameter, input);
			}

			fang::WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

			const fang::MoveIntent intent =
				fang::StepPursuit(m_pursuitParameter, m_blackboard, m_position, deltaTimeSeconds, &m_state);

			const std::span<const fang::Contact> contacts =
				(m_world != nullptr) ? m_world->GetContacts() : std::span<const fang::Contact>{};

			const fang::ContactMoveResult moveResult =
				fang::MoveWithContacts(m_position, intent.desiredDelta, contacts, self.index);
			m_position = moveResult.position;

			(void)scene.SetLocalTransform(self, m_position, 0.0f);
		}


	private:
		fang::CollisionWorld*     m_world = nullptr;
		fang::ActorHandle         m_target;
		fang::PerceptionParameter m_perceptionParameter;
		fang::PursuitParameter    m_pursuitParameter;

		fang::Vector3         m_position;
		fang::AgentBlackboard m_blackboard;
		fang::EnPursuitState  m_state = fang::EnPursuitState::Idle;
	};


	/** @brief 関数を呼ぶのにかかった秒を測る。 */
	template <typename Function> [[nodiscard]] float MeasureSeconds(Function&& function)
	{
		const auto start = std::chrono::steady_clock::now();
		function();
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
	}
} // namespace


TEST_CASE("EnemyEncounter: 32体まで湧かせて600フレーム回してもヒープ確保が増えない")
{
	CountingAllocator allocator;

	fang::Scene scene;
	CHECK(scene.Initialize(allocator, fang::SceneDesc{ .maxObjectCount = 40, .maxBehaviorCount = 40 }));

	const uint32_t allocationCountAfterInitialize = allocator.GetAllocationCount();

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	fang::FrameAllocator frameAllocator;
	CHECK(frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"));

	const fang::ActorHandle target = scene.CreateObject();

	fang::SpawnScheduler scheduler;
	fang::SpawnParameter spawnParameter{};
	spawnParameter.intervalSeconds   = 0.05f; // テストを速く進めるため短くする(実際は 1 秒)。
	spawnParameter.maximumAliveCount = 32;

	const fang::PerceptionParameter perceptionParameter{};
	const fang::PursuitParameter    pursuitParameter{};

	uint32_t aliveCount = 0;

	for (int frame = 0; frame < 600; ++frame)
	{
		constexpr float deltaTimeSeconds = 1.0f / 60.0f;

		const fang::SpawnRequest request =
			scheduler.Update(deltaTimeSeconds, aliveCount, fang::Vector3{}, spawnParameter);
		if (request.shouldSpawn)
		{
			const fang::ActorHandle handle = scene.CreateObject();
			if (handle.IsValid())
			{
				fang::IComponent* behavior = scene.AddBehavior<TestEnemyController>(
					handle,
					&world,
					target,
					perceptionParameter,
					pursuitParameter,
					request.position
				);
				if (behavior != nullptr)
				{
					++aliveCount;
				}
			}
		}

		scene.Update(deltaTimeSeconds);

		const std::span<const fang::ColliderProxy> colliderProxies = scene.BuildColliderProxies(frameAllocator);
		world.Update(colliderProxies);
	}

	CHECK(aliveCount == spawnParameter.maximumAliveCount);
	CHECK(allocator.GetAllocationCount() == allocationCountAfterInitialize);

	frameAllocator.Shutdown();
	world.Shutdown();
	scene.Shutdown();
}


TEST_CASE("EnemyEncounter: 74登録+32体の感知・追跡が実機予算の1割に収まる見込み")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t PROP_COUNT  = 40;
	constexpr uint32_t WOLF_COUNT  = 2;
	constexpr uint32_t ENEMY_COUNT = 32;

	constexpr uint32_t WOLF_USER_INDEX_BASE  = PROP_COUNT;              // 40, 41
	constexpr uint32_t ENEMY_USER_INDEX_BASE = PROP_COUNT + WOLF_COUNT; // 42..73

	// 置き物 40 + 狼 2 + 雑魚 32 = 74。置き物は OBB、狼と雑魚はカプセルを模す。
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
				.userIndex = ENEMY_USER_INDEX_BASE + index,
			}
		);
	}

	world.Update(proxies);
	CHECK(world.GetColliderCount() == PROP_COUNT + WOLF_COUNT + ENEMY_COUNT);

	const fang::PerceptionParameter perceptionParameter{};
	const fang::PursuitParameter    pursuitParameter{};
	const fang::Vector3             targetPosition{ 0.0f, 0.0f, 500.0f };

	std::vector<fang::AgentBlackboard> blackboards(ENEMY_COUNT);
	std::vector<fang::EnPursuitState>  states(ENEMY_COUNT, fang::EnPursuitState::Chase);
	for (uint32_t index = 0; index < ENEMY_COUNT; ++index)
	{
		blackboards[index].isTargetVisible        = true;
		blackboards[index].hasLastSeenPosition    = true;
		blackboards[index].lastSeenTargetPosition = targetPosition;
	}

	const float measuredSeconds = MeasureSeconds([&]() {
		for (uint32_t index = 0; index < ENEMY_COUNT; ++index)
		{
			const fang::PerceptionInput input{
				.selfPosition      = enemyPositions[index],
				.selfFacingRadians = 0.0f,
				.targetPosition    = targetPosition,
				.selfUserIndex     = ENEMY_USER_INDEX_BASE + index,
				.targetUserIndex   = WOLF_USER_INDEX_BASE,
			};
			const fang::PerceptionResult result = fang::Sense(world, perceptionParameter, input);
			fang::WritePerception(result, targetPosition, 1.0f / 60.0f, &blackboards[index]);

			(void)fang::StepPursuit(
				pursuitParameter,
				blackboards[index],
				enemyPositions[index],
				1.0f / 60.0f,
				&states[index]
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
