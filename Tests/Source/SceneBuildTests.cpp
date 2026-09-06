/**
 * @file SceneBuildTests.cpp
 * @brief BuildRenderItems / BuildColliderProxies のテスト。コンポーネントの有無が混ざっても崩れないこと、
 *        userIndex から持ち主を引けること、生成・破棄・更新・組み立てでヒープ確保が 0 であることを確かめる。
 */
#include "Collision/CollisionWorld.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Renderer/MeshRenderer.h"
#include "Scene/Scene.h"
#include <doctest.h>


namespace
{
	/** @brief 呼ばれた回数を数えるだけのアロケータ。Scene の実行中のヒープ確保が 0 であることの確認に使う。 */
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

	/** @brief 何もしない振る舞い。ブロックプールの生成・破棄の経路だけを通すために使う。 */
	class NoOpBehavior final : public fang::IComponent
	{
	public:
		void Update(float /*deltaTimeSeconds*/, fang::Actor /*self*/) override {}
	};

	/** @brief min/max を指定した Aabb を作る。 */
	fang::Aabb MakeAabb(const fang::Vector3& min, const fang::Vector3& max)
	{
		fang::Aabb bounds;
		bounds.Expand(min);
		bounds.Expand(max);
		return bounds;
	}
} // namespace


TEST_CASE("SceneBuild: コンポーネントを持たないオブジェクトが混ざっても崩れず、userIndex から持ち主を引ける")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 8 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const fang::ActorHandle withMesh     = scene.CreateObject();
	const fang::ActorHandle bare         = scene.CreateObject(); // Transform だけで何も持たない。
	const fang::ActorHandle withCollider = scene.CreateObject();

	fang::MeshRendererComponent meshComponent{};
	meshComponent.localBounds = MakeAabb({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });
	CHECK(scene.AddMeshRendererComponent(withMesh, meshComponent));

	fang::ColliderComponent colliderComponent{};
	colliderComponent.shapeType   = fang::EnShapeType::OBB;
	colliderComponent.localBounds = MakeAabb({ -2.0f, -2.0f, -2.0f }, { 2.0f, 2.0f, 2.0f });
	CHECK(scene.AddColliderComponent(withCollider, colliderComponent));

	CHECK_FALSE(scene.GetMeshRendererComponent(bare) != nullptr);
	CHECK_FALSE(scene.GetColliderComponent(bare) != nullptr);

	scene.Update(0.0f);

	fang::FrameAllocator frameAllocator;
	if (!frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	const std::span<const fang::RenderItem>    renderItems     = scene.BuildRenderItems(frameAllocator);
	const std::span<const fang::ColliderProxy> colliderProxies = scene.BuildColliderProxies(frameAllocator);

	CHECK(renderItems.size() == 1);
	CHECK(colliderProxies.size() == 1);
	CHECK(colliderProxies[0].userIndex == withCollider.index);

	// 値を入れていない登録は既定の全ビット ➡ 既存の押し出しが黙って効かなくなることはない。
	CHECK(colliderProxies[0].attributeMask == fang::ALL_COLLISION_ATTRIBUTE_MASK);

	frameAllocator.Shutdown();
	scene.Shutdown();
}


TEST_CASE("ColliderComponent の attributeMask が ColliderProxy にそのまま写る")
{
	fang::Scene scene;
	if (!scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	constexpr uint32_t CUSTOM_ATTRIBUTE = 1u << 3;

	const fang::ActorHandle object = scene.CreateObject();

	fang::ColliderComponent colliderComponent{};
	colliderComponent.shapeType     = fang::EnShapeType::OBB;
	colliderComponent.localBounds   = MakeAabb({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });
	colliderComponent.attributeMask = CUSTOM_ATTRIBUTE;
	CHECK(scene.AddColliderComponent(object, colliderComponent));

	scene.Update(0.0f);

	fang::FrameAllocator frameAllocator;
	if (!frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	const std::span<const fang::ColliderProxy> colliderProxies = scene.BuildColliderProxies(frameAllocator);
	CHECK(colliderProxies.size() == 1);
	CHECK(colliderProxies[0].attributeMask == CUSTOM_ATTRIBUTE);

	frameAllocator.Shutdown();
	scene.Shutdown();
}


TEST_CASE("SceneBuild: 生成・破棄・更新・組み立てでヒープ確保が 0")
{
	CountingAllocator allocator;

	fang::Scene scene;
	if (!scene.Initialize(allocator, fang::SceneDesc{ .maxObjectCount = 8, .maxBehaviorCount = 4 }))
	{
		CHECK_MESSAGE(false, "Scene を初期化できなかった");
		return;
	}

	const uint32_t allocationCountAfterInitialize = allocator.GetAllocationCount();

	fang::FrameAllocator frameAllocator;
	if (!frameAllocator.Initialize(fang::HeapAllocator::GetInstance(), 64 * 1024, "Test"))
	{
		CHECK_MESSAGE(false, "フレームアロケータを初期化できなかった");
		return;
	}

	const fang::ActorHandle object = scene.CreateObject();

	fang::MeshRendererComponent meshComponent{};
	meshComponent.localBounds = MakeAabb({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });
	CHECK(scene.AddMeshRendererComponent(object, meshComponent));

	fang::ColliderComponent colliderComponent{};
	colliderComponent.localBounds = meshComponent.localBounds;
	CHECK(scene.AddColliderComponent(object, colliderComponent));

	CHECK(scene.AddBehavior<NoOpBehavior>(object) != nullptr);

	scene.Update(1.0f / 60.0f);
	CHECK(scene.BuildRenderItems(frameAllocator).size() == 1);
	CHECK(scene.BuildColliderProxies(frameAllocator).size() == 1);

	scene.DestroyObject(object);
	scene.Update(1.0f / 60.0f);

	CHECK(allocator.GetAllocationCount() == allocationCountAfterInitialize);

	frameAllocator.Shutdown();
	scene.Shutdown();
}
