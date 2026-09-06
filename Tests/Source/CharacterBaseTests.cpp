/**
 * @file CharacterBaseTests.cpp
 * @brief CharacterBase のテスト。接地・押し戻し・向き詰め・大きさを確かめる。
 */
#include "Collision/Collision.h"
#include "Core/Math/MathConstants.h"
#include "Core/Memory/Allocator.h"
#include "Resource/HeightmapTerrain.h"
#include "Scene/CharacterBase.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <vector>


namespace
{
	/** @brief CharacterBase の protected な操作をテストから呼べるようにするだけの派生。 */
	class TestCharacter final : public fang::CharacterBase
	{
	public:
		TestCharacter(
			const GroundDependencies& dependencies,
			const fang::Vector3&      initialPosition,
			float                     initialFacingRadians
		)
			: fang::CharacterBase(dependencies, initialPosition, initialFacingRadians)
		{
		}

		/** @brief 振る舞いとしては使わない（IComponent を満たすためだけ）。 */
		void Update(float /*deltaTimeSeconds*/, fang::Actor /*self*/) override {}

		using fang::CharacterBase::GroundDependencies;
		using fang::CharacterBase::MovePosition;
		using fang::CharacterBase::TurnFacingTowards;
		using fang::CharacterBase::WriteTransform;
	};

	/** @brief 平坦なハイトマップを outTerrain へ作る（HeightmapTerrain はコピーもムーブもできないため out 引数）。 */
	void BuildFlatTerrain(float heightCentimeters, fang::HeightmapTerrain* outTerrain)
	{
		const uint16_t pixel = static_cast<uint16_t>((heightCentimeters / 100.0f) * 65535.0f);

		CHECK(outTerrain->BuildFromHeights(
			std::vector<uint16_t>(9, pixel),
			3,
			3,
			fang::HeightmapTerrainDesc{ .totalWidth = 1000.0f, .totalDepth = 1000.0f, .heightScale = 100.0f }
		));
	}
} // namespace


TEST_CASE("CharacterBase: WriteTransform は足元へ地表の高さを足す")
{
	fang::HeightmapTerrain terrain;
	BuildFlatTerrain(42.0f, &terrain);

	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }));

	const fang::ActorHandle handle = scene.CreateObject();
	const fang::Actor       actor(scene, handle);

	TestCharacter character(
		TestCharacter::GroundDependencies{ .collisionWorld = nullptr, .terrain = &terrain },
		fang::Vector3{ 10.0f, 0.0f, 20.0f },
		0.0f
	);
	character.WriteTransform(actor);
	scene.Update(0.0f);

	const fang::Vector3 worldPosition = actor.GetWorldPosition();
	CHECK(worldPosition.x == doctest::Approx(10.0f));
	CHECK(worldPosition.y == doctest::Approx(42.0f).epsilon(0.001)); // 16bit ハイトマップの量子化ぶんの誤差を許す。
	CHECK(worldPosition.z == doctest::Approx(20.0f));

	scene.Shutdown();
}


TEST_CASE("CharacterBase: 地形が無ければ y = 0 に立つ")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }));

	const fang::ActorHandle handle = scene.CreateObject();
	const fang::Actor       actor(scene, handle);

	TestCharacter character(
		TestCharacter::GroundDependencies{ .collisionWorld = nullptr, .terrain = nullptr },
		fang::Vector3{ 5.0f, 0.0f, 5.0f },
		0.0f
	);
	character.WriteTransform(actor);
	scene.Update(0.0f);

	CHECK(actor.GetWorldPosition().y == doctest::Approx(0.0f));

	scene.Shutdown();
}


TEST_CASE("CharacterBase: 当たり判定が無ければ MovePosition はそのまま進む")
{
	TestCharacter character(
		TestCharacter::GroundDependencies{ .collisionWorld = nullptr, .terrain = nullptr },
		fang::Vector3{ 0.0f, 0.0f, 0.0f },
		0.0f
	);

	const fang::Vector3 appliedDelta = character.MovePosition(fang::Vector3{ 30.0f, 0.0f, 0.0f }, 0);
	CHECK(appliedDelta.x == doctest::Approx(30.0f));
	CHECK(character.GetPosition().x == doctest::Approx(30.0f));
}


TEST_CASE("CharacterBase: MovePosition は壁に食い込む成分を削る（押し戻し）")
{
	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t SELF_USER_INDEX = 0;
	constexpr uint32_t WALL_USER_INDEX = 1;

	// 自分は原点のカプセル（x で -20 〜 20）、壁は x = 0 〜 30 の OBB ➡ 深く重なっている。
	const std::vector<fang::ColliderProxy> proxies{
		fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::Capsule{ .pointA = fang::Vector3{},
							   .pointB = fang::Vector3{ 0.0f, 100.0f, 0.0f },
							   .radius = 20.0f }
			),
			.userIndex = SELF_USER_INDEX,
		},
		fang::ColliderProxy{
			.shape = fang::MakeColliderShape(
				fang::OBB{ .center = fang::Vector3{ 15.0f, 50.0f, 0.0f }, .halfExtents = { 15.0f, 50.0f, 100.0f } }
			),
			.userIndex = WALL_USER_INDEX,
		},
	};
	world.Update(proxies);
	CHECK(world.GetContacts().size() >= 1);

	TestCharacter character(
		TestCharacter::GroundDependencies{ .collisionWorld = &world, .terrain = nullptr },
		fang::Vector3{ 0.0f, 0.0f, 0.0f },
		0.0f
	);

	// +X へ進みたいが、壁がその向きにある ➡ 実際に進んだ量は望んだ量より小さくなる。
	const fang::Vector3 appliedDelta = character.MovePosition(fang::Vector3{ 10.0f, 0.0f, 0.0f }, SELF_USER_INDEX);
	CHECK(appliedDelta.x < 10.0f);

	world.Shutdown();
}


TEST_CASE("CharacterBase: TurnFacingTowards は最大 maxStepRadians だけ向きを近づける")
{
	TestCharacter character(
		TestCharacter::GroundDependencies{ .collisionWorld = nullptr, .terrain = nullptr },
		fang::Vector3{},
		0.0f
	);

	character.TurnFacingTowards(1.0f, 0.25f);
	CHECK(character.GetFacingRadians() == doctest::Approx(0.25f));

	character.TurnFacingTowards(1.0f, 1.0f);
	CHECK(character.GetFacingRadians() == doctest::Approx(1.0f));
}


TEST_CASE("CharacterBase: 大きさは CHARACTER_BASE_SIZE_LIMIT に収まる")
{
	CHECK(sizeof(fang::CharacterBase) <= fang::CHARACTER_BASE_SIZE_LIMIT);
}
