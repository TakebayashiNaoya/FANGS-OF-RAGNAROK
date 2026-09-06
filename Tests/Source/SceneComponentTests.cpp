/**
 * @file SceneComponentTests.cpp
 * @brief コンポーネントのテスト。振る舞いの更新順（次の周から / その周で止まる / 二重に回らない）と、
 *        汎用コンポーネントの着脱を確かめる。
 */
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>


namespace
{
	/** @brief 呼ばれるたびに外部のカウンタを 1 増やすだけの振る舞い。 */
	class CountingBehavior final : public fang::IComponent
	{
	public:
		explicit CountingBehavior(int* counter)
			: m_counter(counter)
		{
		}

		void Update(float /*deltaTimeSeconds*/, fang::ActorHandle /*self*/, fang::Scene& /*scene*/) override
		{
			++(*m_counter);
		}


	private:
		int* m_counter = nullptr;
	};

	/** @brief 最初の Update で 1 個だけオブジェクトを作り、CountingBehavior を付ける振る舞い。 */
	class SpawningBehavior final : public fang::IComponent
	{
	public:
		explicit SpawningBehavior(int* spawnedCounter)
			: m_spawnedCounter(spawnedCounter)
		{
		}

		void Update(float /*deltaTimeSeconds*/, fang::ActorHandle /*self*/, fang::Scene& scene) override
		{
			if (m_hasSpawned)
			{
				return;
			}
			m_hasSpawned = true;

			const fang::ActorHandle spawned = scene.CreateObject();
			(void)scene.AddBehavior<CountingBehavior>(spawned, m_spawnedCounter);
		}


	private:
		bool m_hasSpawned     = false;
		int* m_spawnedCounter = nullptr;
	};

	/** @brief 呼ばれるたびに指定したオブジェクトを破棄する振る舞い。 */
	class DestroyingBehavior final : public fang::IComponent
	{
	public:
		explicit DestroyingBehavior(fang::ActorHandle target)
			: m_target(target)
		{
		}

		void Update(float /*deltaTimeSeconds*/, fang::ActorHandle /*self*/, fang::Scene& scene) override
		{
			scene.DestroyObject(m_target);
		}


	private:
		fang::ActorHandle m_target;
	};
} // namespace


TEST_CASE("SceneComponent: 振る舞いは 1 回の Update で 1 回だけ回る")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	int counter = 0;

	const fang::ActorHandle object = scene.CreateObject();
	CHECK(scene.AddBehavior<CountingBehavior>(object, &counter) != nullptr);

	scene.Update(0.0f);
	CHECK(counter == 1);

	scene.Update(0.0f);
	CHECK(counter == 2);

	scene.Update(0.0f);
	CHECK(counter == 3);

	scene.Shutdown();
}


TEST_CASE("SceneComponent: 更新中に足した振る舞いは次の周から回る")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	int spawnedCounter = 0;

	const fang::ActorHandle spawner = scene.CreateObject();
	CHECK(scene.AddBehavior<SpawningBehavior>(spawner, &spawnedCounter) != nullptr);

	scene.Update(0.0f); // ここで新しいオブジェクトと振る舞いが増える。
	CHECK(spawnedCounter == 0);

	scene.Update(0.0f); // 次の周から数え始める。
	CHECK(spawnedCounter == 1);

	scene.Update(0.0f);
	CHECK(spawnedCounter == 2);

	scene.Shutdown();
}


TEST_CASE("SceneComponent: 更新中に破棄されたオブジェクトの振る舞いはその周で止まる")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	int victimCounter = 0;

	const fang::ActorHandle victim    = scene.CreateObject();
	const fang::ActorHandle destroyer = scene.CreateObject();

	// 先に登録した振る舞いから回るので、destroyer を victim より先に登録しておく
	// ➡ victim の番が来る前に破棄予約が立ち、その周のうちに止まることを確かめられる。
	CHECK(scene.AddBehavior<DestroyingBehavior>(destroyer, victim) != nullptr);
	CHECK(scene.AddBehavior<CountingBehavior>(victim, &victimCounter) != nullptr);

	scene.Update(0.0f);
	CHECK(victimCounter == 0);
	CHECK_FALSE(scene.IsValid(victim));

	scene.Update(0.0f);
	CHECK(victimCounter == 0); // 既に消えているので増えない。

	scene.Shutdown();
}


TEST_CASE("SceneComponent: 汎用コンポーネントは 1 個まで持てる")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle object = scene.CreateObject();

	CHECK(scene.GetMeshRendererComponent(object) == nullptr);
	CHECK(scene.AddMeshRendererComponent(object, fang::MeshRendererComponent{}));
	CHECK(scene.GetMeshRendererComponent(object) != nullptr);
	CHECK_FALSE(scene.AddMeshRendererComponent(object, fang::MeshRendererComponent{})); // 2 個目は拒否される。

	CHECK(scene.GetColliderComponent(object) == nullptr);
	CHECK(scene.AddColliderComponent(object, fang::ColliderComponent{}));
	CHECK(scene.GetColliderComponent(object) != nullptr);
	CHECK_FALSE(scene.AddColliderComponent(object, fang::ColliderComponent{}));

	scene.DestroyObject(object);
	scene.Update(0.0f);

	// 破棄されたオブジェクトの分だけコンポーネントも畳まれる（別のオブジェクトが持つ分に影響しない）。
	const fang::ActorHandle other = scene.CreateObject();
	CHECK(scene.AddMeshRendererComponent(other, fang::MeshRendererComponent{}));
	CHECK(scene.GetMeshRendererComponent(other) != nullptr);

	scene.Shutdown();
}
