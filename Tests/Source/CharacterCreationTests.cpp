/**
 * @file CharacterCreationTests.cpp
 * @brief CharacterDesc / CreateCharacter のテスト。記述子の違い・mesh 無効・巻き戻しを確かめる。
 */
#include "Core/Memory/Allocator.h"
#include "Scene/CharacterDesc.h"
#include "Scene/Scene.h"
#include <doctest.h>


namespace
{
	/** @brief 振る舞いとしては何もしない、生成のテストだけに使うダミー。 */
	class NoOpBehavior final : public fang::IComponent
	{
	public:
		void Update(float /*deltaTimeSeconds*/, fang::Actor /*self*/) override {}
	};

	/** @brief 成分ごとに一致することを見る。 */
	void CheckAabbEqual(const fang::Aabb& actual, const fang::Aabb& expected)
	{
		CHECK(actual.min.x == expected.min.x);
		CHECK(actual.min.y == expected.min.y);
		CHECK(actual.min.z == expected.min.z);
		CHECK(actual.max.x == expected.max.x);
		CHECK(actual.max.y == expected.max.y);
		CHECK(actual.max.z == expected.max.z);
	}

	/** @brief mesh を持つ記述子。attributeMask だけを差し替えて使う。 */
	[[nodiscard]] fang::CharacterDesc MakeBaseDesc(uint32_t attributeMask)
	{
		return fang::CharacterDesc{
			.renderer =
				fang::MeshRendererComponent{
					.mesh        = fang::MeshId{ .index = 0 },
					.localBounds = fang::Aabb{ .min = { -10.0f, 0.0f, -10.0f }, .max = { 10.0f, 100.0f, 10.0f } },
				},
			.shapeType     = fang::EnShapeType::Capsule,
			.attributeMask = attributeMask,
			.health        = fang::HealthComponent{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f },
		};
	}
} // namespace


TEST_CASE("CharacterCreation: 記述子の違いだけで 3 種類のキャラクターを作れる")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 8 }));

	constexpr uint32_t ATTRIBUTE_MASKS[3] = { 1u << 0, 1u << 1, 1u << 2 };

	const fang::CharacterDesc referenceDesc = MakeBaseDesc(ATTRIBUTE_MASKS[0]);

	for (const uint32_t attributeMask : ATTRIBUTE_MASKS)
	{
		const fang::CharacterDesc desc = MakeBaseDesc(attributeMask);

		const fang::CharacterCreateResult<NoOpBehavior> result = fang::CreateCharacter<NoOpBehavior>(scene, desc);

		CHECK(result.actor.IsValid());
		CHECK(result.behavior != nullptr);

		fang::MeshRendererComponent* renderer = result.actor.GetMeshRendererComponent();
		fang::ColliderComponent*     collider = result.actor.GetColliderComponent();
		fang::HealthComponent*       health   = result.actor.GetHealthComponent();

		CHECK(renderer != nullptr);
		CHECK(collider != nullptr);
		CHECK(health != nullptr);

		CHECK(collider->attributeMask == attributeMask);
		CHECK(collider->shapeType == referenceDesc.shapeType);
		CheckAabbEqual(collider->localBounds, referenceDesc.renderer.localBounds);
	}

	scene.Shutdown();
}


TEST_CASE("CharacterCreation: mesh が無効なら見た目もコライダーも足さない")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }));

	const fang::CharacterDesc desc{
		.health = fang::HealthComponent{ .maximumHitPoints = 50.0f, .currentHitPoints = 50.0f },
	};

	const fang::Actor actor = fang::CreateCharacterActor(scene, desc);

	CHECK(actor.IsValid());
	CHECK(actor.GetMeshRendererComponent() == nullptr);
	CHECK(actor.GetColliderComponent() == nullptr);
	CHECK(actor.GetHealthComponent() != nullptr);

	scene.Shutdown();
}


TEST_CASE("CharacterCreation: AddBehavior が失敗したら作りかけを残さない")
{
	fang::Scene scene;
	CHECK(scene.Initialize(
		fang::HeapAllocator::GetInstance(),
		fang::SceneDesc{ .maxObjectCount = 4, .maxBehaviorCount = 1 }
	));

	const fang::CharacterDesc desc{
		.health = fang::HealthComponent{ .maximumHitPoints = 10.0f, .currentHitPoints = 10.0f },
	};

	// 1 体目で振る舞いのブロックを使い切る。
	const fang::CharacterCreateResult<NoOpBehavior> first = fang::CreateCharacter<NoOpBehavior>(scene, desc);
	CHECK(first.actor.IsValid());
	CHECK(first.behavior != nullptr);

	const uint32_t activeCountBeforeSecondAttempt = scene.GetActiveObjectCount();

	// 2 体目は AddBehavior が nullptr を返す ➡ 作りかけのオブジェクトは巻き戻されること。
	const fang::CharacterCreateResult<NoOpBehavior> second = fang::CreateCharacter<NoOpBehavior>(scene, desc);
	CHECK_FALSE(second.actor.IsValid());
	CHECK(second.behavior == nullptr);

	scene.Update(0.0f); // 破棄反映。

	CHECK(scene.GetActiveObjectCount() == activeCountBeforeSecondAttempt);

	scene.Shutdown();
}
