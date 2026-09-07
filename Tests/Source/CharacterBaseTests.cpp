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


	/** @brief 狼の実寸の水平カプセル。中心線は体軸（X）、足元 position から地表 groundHeight ぶん持ち上げる。 */
	fang::Capsule MakeWolfCapsule(const fang::Vector3& position, float groundHeight)
	{
		constexpr float CENTER_OFFSET_X     = 9.2f;   // ローカル箱 X の中心。
		constexpr float CENTER_HEIGHT       = 53.1f;  // ローカル箱 Y の中心。
		constexpr float RADIUS              = 18.15f; // Z の半幅。
		constexpr float SEGMENT_HALF_LENGTH = 101.8f - RADIUS;

		const float centerY = position.y + groundHeight + CENTER_HEIGHT;
		return fang::Capsule{
			.pointA = { position.x + CENTER_OFFSET_X - SEGMENT_HALF_LENGTH, centerY, position.z },
			.pointB = { position.x + CENTER_OFFSET_X + SEGMENT_HALF_LENGTH, centerY, position.z },
			.radius = RADIUS,
		};
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


TEST_CASE("CharacterBase: 接触しても足裏は地表に付いたまま")
{
	fang::HeightmapTerrain terrain;
	BuildFlatTerrain(42.0f, &terrain);

	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{ .maxObjectCount = 4 }));

	const fang::ActorHandle handle = scene.CreateObject();
	const fang::Actor       actor(scene, handle);

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	constexpr uint32_t SELF_USER_INDEX  = 0;
	constexpr uint32_t OTHER_USER_INDEX = 1;
	constexpr float    SELF_GROUND      = 42.0f;
	constexpr float    OTHER_GROUND     = SELF_GROUND + 20.0f; // 相手の中心を 20cm 高くする。

	// 相手は自分と同じカプセルを +20cm（Z）へ置く。中心の距離は sqrt(20^2 + 20^2) = 28.284、
	// 深さ = 半径の和(36.3) - 28.284 = 8.016、法線 y = 0.707。
	const fang::ColliderProxy selfProxy{
		.shape     = fang::MakeColliderShape(MakeWolfCapsule(fang::Vector3{}, SELF_GROUND)),
		.userIndex = SELF_USER_INDEX,
	};
	const fang::ColliderProxy otherProxy{
		.shape     = fang::MakeColliderShape(MakeWolfCapsule(fang::Vector3{ 0.0f, 0.0f, 20.0f }, OTHER_GROUND)),
		.userIndex = OTHER_USER_INDEX,
	};
	const fang::ColliderProxy proxies[] = { selfProxy, otherProxy };
	world.Update(proxies);
	CHECK(world.GetContacts().size() >= 1);

	TestCharacter character(
		TestCharacter::GroundDependencies{ .collisionWorld = &world, .terrain = &terrain },
		fang::Vector3{ 0.0f, 0.0f, 0.0f },
		0.0f
	);

	character.MovePosition(fang::Vector3{}, SELF_USER_INDEX);
	CHECK(character.GetPosition().y == doctest::Approx(0.0f));
	// 押し出しは depth - PENETRATION_SKIN_CENTIMETERS = 8.016 - 0.5 = 7.516、向きは -Z。
	CHECK(character.GetPosition().z == doctest::Approx(-7.51573f));

	character.WriteTransform(actor);
	scene.Update(0.0f);

	// 16bit ハイトマップの量子化ぶんの誤差を許す（既存テストと同じ）。
	CHECK(actor.GetWorldPosition().y == doctest::Approx(42.0f).epsilon(0.001));

	world.Shutdown();
	scene.Shutdown();
}
