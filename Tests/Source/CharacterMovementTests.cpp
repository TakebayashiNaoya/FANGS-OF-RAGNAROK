/**
 * @file CharacterMovementTests.cpp
 * @brief 移動と押し戻しのテスト。移動量・向きの詰め・押し出しの解決・壁沿いの滑りを確かめる。
 */
#include "Collision/Collision.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include "Core/Memory/Allocator.h"
#include "Scene/CharacterMovement.h"
#include <doctest.h>
#include <cmath>
#include <vector>


namespace
{
	/** @brief 成分ごとに近いことを見る。 */
	void CheckVector3(const fang::Vector3& actual, const fang::Vector3& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x));
		CHECK(actual.y == doctest::Approx(expected.y));
		CHECK(actual.z == doctest::Approx(expected.z));
	}


	/** @brief 押し出しの 1 件を作る。 */
	fang::PenetrationSample MakeSample(const fang::Vector3& normal, float depth)
	{
		return fang::PenetrationSample{ .normal = normal, .depth = depth };
	}


	/** @brief 接触を 1 件作る。押し出しの向きは A から B へ。 */
	fang::Contact MakeContact(uint32_t userIndexA, uint32_t userIndexB, const fang::Vector3& normal, float depth)
	{
		fang::Contact contact;
		contact.userIndexA = userIndexA;
		contact.userIndexB = userIndexB;
		contact.normal     = normal;
		contact.depth      = depth;

		return contact;
	}
} // namespace


TEST_CASE("スティックとカメラの方位から移動量が決まる")
{
	// カメラが +X を向いているとき、スティックの奥は +X。
	const fang::Vector3 forward = fang::MakeMoveDelta(fang::Vector2{ 0.0f, 1.0f }, 0.0f, 100.0f, 1.0f);
	CheckVector3(forward, fang::Vector3{ 100.0f, 0.0f, 0.0f });

	// 同じカメラで右へ倒すと -Z（左手系 Y-up で画面の右）。
	const fang::Vector3 right = fang::MakeMoveDelta(fang::Vector2{ 1.0f, 0.0f }, 0.0f, 100.0f, 1.0f);
	CheckVector3(right, fang::Vector3{ 0.0f, 0.0f, -100.0f });

	// カメラを 90 度回すと、奥が +Z になる。
	const fang::Vector3 turned = fang::MakeMoveDelta(fang::Vector2{ 0.0f, 1.0f }, fang::PI * 0.5f, 100.0f, 1.0f);
	CheckVector3(turned, fang::Vector3{ 0.0f, 0.0f, 100.0f });

	// 倒していなければ動かない。
	CheckVector3(fang::MakeMoveDelta(fang::Vector2{}, 0.0f, 100.0f, 1.0f), fang::Vector3{});

	// 速さと時間が掛かる。y は常に 0（水平移動しかしない）。
	const fang::Vector3 scaled = fang::MakeMoveDelta(fang::Vector2{ 0.0f, 0.5f }, 0.0f, 400.0f, 0.5f);
	CheckVector3(scaled, fang::Vector3{ 100.0f, 0.0f, 0.0f });
}


TEST_CASE("向きが最短の回り方で目標へ詰まる")
{
	// 届かない距離なら、刻んだぶんだけ進む。
	CHECK(fang::TurnTowards(0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
	CHECK(fang::TurnTowards(0.0f, -1.0f, 0.25f) == doctest::Approx(-0.25f));

	// 届く距離なら目標そのもの。
	CHECK(fang::TurnTowards(0.0f, 0.1f, 0.25f) == doctest::Approx(0.1f));

	// ±π を跨ぐとき、遠回りしない。3.0 から -3.0 へは +0.283 進むのが最短。
	const float wrapped = fang::TurnTowards(3.0f, -3.0f, 0.1f);
	CHECK(wrapped == doctest::Approx(3.1f));

	// 戻り値は必ず -π 〜 π に収まる。
	const float folded = fang::TurnTowards(3.1f, -3.0f, 0.1f);
	CHECK(folded <= fang::PI);
	CHECK(folded >= -fang::PI);
}


TEST_CASE("向きベクトルと角度が往復する")
{
	CHECK(fang::GetYawFromDirection(fang::Vector3{ 1.0f, 0.0f, 0.0f }) == doctest::Approx(0.0f));
	CHECK(fang::GetYawFromDirection(fang::Vector3{ 0.0f, 0.0f, 1.0f }) == doctest::Approx(fang::PI * 0.5f));
	CHECK(fang::GetYawFromDirection(fang::Vector3{ -1.0f, 0.0f, 0.0f }) == doctest::Approx(fang::PI));

	// 高さの成分は向きに影響しない。
	CHECK(fang::GetYawFromDirection(fang::Vector3{ 1.0f, 99.0f, 0.0f }) == doctest::Approx(0.0f));

	// 水平成分が無ければ 0（長さ 0 のベクトルの角度を作らない）。
	CHECK(fang::GetYawFromDirection(fang::Vector3{ 0.0f, 1.0f, 0.0f }) == doctest::Approx(0.0f));
}


TEST_CASE("接触から自分を外へ出す向きが取れる")
{
	// 自分が 1 つ目の側。法線は A から B へ押す向きなので、自分を出すには反転する。
	const fang::Contact asFirst = MakeContact(7, 9, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 3.0f);

	fang::PenetrationSample samples[fang::MAX_PENETRATION_SAMPLE_COUNT]{};
	CHECK(fang::CollectPenetrations(std::span<const fang::Contact>(&asFirst, 1), 7, samples) == 1);
	CheckVector3(samples[0].normal, fang::Vector3{ -1.0f, 0.0f, 0.0f });
	CHECK(samples[0].depth == doctest::Approx(3.0f));

	// 自分が 2 つ目の側なら、法線はそのまま。
	CHECK(fang::CollectPenetrations(std::span<const fang::Contact>(&asFirst, 1), 9, samples) == 1);
	CheckVector3(samples[0].normal, fang::Vector3{ 1.0f, 0.0f, 0.0f });

	// 自分が関わっていない接触は拾わない。
	CHECK(fang::CollectPenetrations(std::span<const fang::Contact>(&asFirst, 1), 100, samples) == 0);

	// 書き込み先が足りなければ打ち切る。
	std::vector<fang::Contact> many;
	for (uint32_t index = 0; index < 20; ++index)
	{
		many.push_back(MakeContact(7, index + 100, fang::Vector3{ 1.0f, 0.0f, 0.0f }, 1.0f));
	}

	fang::PenetrationSample smallTarget[2]{};
	CHECK(fang::CollectPenetrations(many, 7, smallTarget) == 2);
}


TEST_CASE("押し出しが深さを解消し、同じ向きを二重に押さない")
{
	// 1 面。余白ぶんだけ重なりを残して押し出す。
	const fang::PenetrationSample single[] = { MakeSample(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 4.0f) };
	CheckVector3(
		fang::ResolvePenetration(single),
		fang::Vector3{ 4.0f - fang::PENETRATION_SKIN_CENTIMETERS, 0.0f, 0.0f }
	);

	// 同じ向きの接触が 3 件あっても、いちばん深いものぶんしか押さない。
	const fang::PenetrationSample sameDirection[] = {
		MakeSample(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 2.0f),
		MakeSample(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 4.0f),
		MakeSample(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 1.0f),
	};
	CheckVector3(
		fang::ResolvePenetration(sameDirection),
		fang::Vector3{ 4.0f - fang::PENETRATION_SKIN_CENTIMETERS, 0.0f, 0.0f }
	);

	// 直交する 2 面（角）。どちらも解消され、飛ばされない。
	const fang::PenetrationSample corner[] = {
		MakeSample(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 2.0f),
		MakeSample(fang::Vector3{ 0.0f, 0.0f, 1.0f }, 3.0f),
	};
	const fang::Vector3 resolved = fang::ResolvePenetration(corner);
	CHECK(resolved.x == doctest::Approx(2.0f - fang::PENETRATION_SKIN_CENTIMETERS));
	CHECK(resolved.z == doctest::Approx(3.0f - fang::PENETRATION_SKIN_CENTIMETERS));

	// 押し出した後、どの面も余白ぶんの重なりまで浅くなっている。
	for (const fang::PenetrationSample& sample : corner)
	{
		CHECK(fang::Dot(resolved, sample.normal) >= sample.depth - fang::PENETRATION_SKIN_CENTIMETERS);
	}

	// 余白より浅い重なりは押し出さない ➡ 接触が消えない。
	const fang::PenetrationSample shallow[] = { MakeSample(fang::Vector3{ 1.0f, 0.0f, 0.0f }, 0.2f) };
	CheckVector3(fang::ResolvePenetration(shallow), fang::Vector3{});

	// 触れていなければ動かさない。
	CheckVector3(fang::ResolvePenetration(std::span<const fang::PenetrationSample>{}), fang::Vector3{});
}


TEST_CASE("壁へ食い込む成分だけが削られる")
{
	// 壁は +X 側にあり、外へ出る向きは -X。+X へ進もうとすると止まる。
	const fang::PenetrationSample wall[] = { MakeSample(fang::Vector3{ -1.0f, 0.0f, 0.0f }, 1.0f) };

	CheckVector3(fang::SlideAlongNormals(fang::Vector3{ 10.0f, 0.0f, 0.0f }, wall), fang::Vector3{});

	// 斜めに進むと、壁に沿った成分だけが残る（滑る）。
	CheckVector3(fang::SlideAlongNormals(fang::Vector3{ 10.0f, 0.0f, 5.0f }, wall), fang::Vector3{ 0.0f, 0.0f, 5.0f });

	// 壁から離れる向きはそのまま通る。
	CheckVector3(
		fang::SlideAlongNormals(fang::Vector3{ -10.0f, 0.0f, 0.0f }, wall),
		fang::Vector3{ -10.0f, 0.0f, 0.0f }
	);

	// 直角の角に入ると、どちらの向きへも進めなくなる。
	const fang::PenetrationSample corner[] = {
		MakeSample(fang::Vector3{ -1.0f, 0.0f, 0.0f }, 1.0f),
		MakeSample(fang::Vector3{ 0.0f, 0.0f, -1.0f }, 1.0f),
	};
	CheckVector3(fang::SlideAlongNormals(fang::Vector3{ 10.0f, 0.0f, 10.0f }, corner), fang::Vector3{});

	// 触れていなければ何も削らない。
	CheckVector3(
		fang::SlideAlongNormals(fang::Vector3{ 3.0f, 0.0f, 4.0f }, std::span<const fang::PenetrationSample>{}),
		fang::Vector3{ 3.0f, 0.0f, 4.0f }
	);
}


TEST_CASE("押し出しと削りを対にすると壁際で振動しない")
{
	// x = 100 に壁がある状況を 120 フレームぶん回す。毎フレーム、その位置から接触を作り直して
	// 「押し出す ➡ 進入方向を削って進む」の順に処理する（実装と同じ並び）。
	constexpr float     WALL_X = 100.0f;
	const fang::Vector3 outward{ -1.0f, 0.0f, 0.0f };

	fang::Vector3 position;
	fang::Vector3 previousPosition;
	float         largestJump = 0.0f;

	for (int frame = 0; frame < 120; ++frame)
	{
		previousPosition = position;

		fang::PenetrationSample samples[1];
		uint32_t                sampleCount = 0;
		if (position.x > WALL_X)
		{
			samples[0]  = MakeSample(outward, position.x - WALL_X);
			sampleCount = 1;
		}

		const std::span<const fang::PenetrationSample> touching(samples, sampleCount);

		position += fang::ResolvePenetration(touching);

		const fang::Vector3 delta = fang::MakeMoveDelta(fang::Vector2{ 0.0f, 1.0f }, 0.0f, 400.0f, 1.0f / 60.0f);
		position += fang::SlideAlongNormals(delta, touching);

		// 壁に着いた後の 1 フレームの動きを見る（20 フレームで 6.67 cm × 20 = 133 cm 進み、必ず着いている）。
		if (frame >= 30)
		{
			const float jump = std::abs(position.x - previousPosition.x);
			largestJump      = (jump > largestJump) ? jump : largestJump;
		}
	}

	// 壁に着いた後は 1 フレームも動かない ➡ 押し出しと再突入をくり返していない。
	CHECK(largestJump == doctest::Approx(0.0f));

	// 壁を越えていない。重なりは余白ぶんまでに収まっている。
	CHECK(position.x <= WALL_X + fang::PENETRATION_SKIN_CENTIMETERS + 0.001f);
}


TEST_CASE("MoveWithContacts が既存の3本の組み合わせと同じ答えになる")
{
	const fang::Vector3 position{ 10.0f, 0.0f, 0.0f };
	const fang::Vector3 desiredDelta{ 5.0f, 0.0f, 3.0f };

	const fang::Contact contacts[] = {
		MakeContact(0, 1, fang::Vector3{ -1.0f, 0.0f, 0.0f }, 2.0f),
		MakeContact(2, 0, fang::Vector3{ 0.0f, 0.0f, 1.0f }, 1.0f),
	};

	constexpr uint32_t SELF_USER_INDEX = 0;

	// 既存の 3 本を手で並べた場合。
	fang::PenetrationSample samples[fang::MAX_PENETRATION_SAMPLE_COUNT]{};
	const uint32_t          sampleCount = fang::CollectPenetrations(contacts, SELF_USER_INDEX, samples);
	const std::span<const fang::PenetrationSample> touching(samples, sampleCount);

	const fang::Vector3 expectedPosition     = position + fang::ResolvePenetration(touching);
	const fang::Vector3 expectedAppliedDelta = fang::SlideAlongNormals(desiredDelta, touching);

	const fang::ContactMoveResult result = fang::MoveWithContacts(position, desiredDelta, contacts, SELF_USER_INDEX);

	CheckVector3(result.appliedDelta, expectedAppliedDelta);
	CheckVector3(result.position, expectedPosition + expectedAppliedDelta);
}


TEST_CASE("MoveWithContacts が実物の CollisionWorld でも壁の手前で止まる")
{
	constexpr uint32_t WOLF_INDEX = 0;
	constexpr uint32_t WALL_INDEX = 1;

	constexpr float WALL_X      = 300.0f;
	constexpr float WALL_HALF   = 100.0f;
	constexpr float WOLF_RADIUS = 20.0f;
	constexpr float DELTA_TIME  = 1.0f / 60.0f;
	constexpr float MOVE_SPEED  = 400.0f;

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	const fang::ColliderProxy wall{
		.shape = fang::MakeColliderShape(
			fang::OBB{ .center = { WALL_X + WALL_HALF, 50.0f, 0.0f }, .halfExtents = { WALL_HALF, 50.0f, 400.0f } }
		),
		.userIndex = WALL_INDEX,
	};

	fang::Vector3 position;

	for (int frame = 0; frame < 120; ++frame)
	{
		const fang::Vector3 desiredDelta =
			fang::MakeMoveDelta(fang::Vector2{ 0.0f, 1.0f }, 0.0f, MOVE_SPEED, DELTA_TIME);

		const fang::ContactMoveResult result =
			fang::MoveWithContacts(position, desiredDelta, world.GetContacts(), WOLF_INDEX);
		position = result.position;

		const fang::ColliderProxy wolfProxy{
			.shape = fang::MakeColliderShape(
				fang::Capsule{
					.pointA = { position.x, position.y + WOLF_RADIUS, position.z },
					.pointB = { position.x, position.y + 80.0f, position.z },
					.radius = WOLF_RADIUS,
				}
			),
			.userIndex = WOLF_INDEX,
		};

		const fang::ColliderProxy proxies[] = { wolfProxy, wall };
		world.Update(proxies);
	}

	CHECK(position.x <= WALL_X - WOLF_RADIUS + fang::PENETRATION_SKIN_CENTIMETERS + 0.5f);
	CHECK(position.x > WALL_X - WOLF_RADIUS - 5.0f);

	world.Shutdown();
}


TEST_CASE("当たり判定と組み合わせても壁を抜けず、壁沿いに滑る")
{
	// Application.cpp の区画 3 と同じ並びを、実物の CollisionWorld で回す。
	// 「押し出す ➡ 進入方向を削る ➡ 進む ➡ その位置で登録し直す」の順。
	constexpr uint32_t WOLF_INDEX = 0;
	constexpr uint32_t WALL_INDEX = 1;

	constexpr float WALL_X      = 300.0f;
	constexpr float WALL_HALF   = 100.0f;
	constexpr float WOLF_RADIUS = 20.0f;
	constexpr float DELTA_TIME  = 1.0f / 60.0f;
	constexpr float MOVE_SPEED  = 400.0f;

	fang::CollisionWorld world;
	CHECK(world.Initialize(fang::HeapAllocator::GetInstance(), fang::CollisionWorldDesc{}));

	// 壁は動かない大きな箱。狼は立った姿勢のカプセル。
	const fang::ColliderProxy wall{
		.shape = fang::MakeColliderShape(
			fang::OBB{ .center = { WALL_X + WALL_HALF, 50.0f, 0.0f }, .halfExtents = { WALL_HALF, 50.0f, 400.0f } }
		),
		.userIndex = WALL_INDEX,
	};

	fang::CharacterMovementState wolf;

	auto RunFrame = [&](const fang::Vector2& stick) {
		fang::PenetrationSample penetrations[fang::MAX_PENETRATION_SAMPLE_COUNT]{};
		const uint32_t penetrationCount = fang::CollectPenetrations(world.GetContacts(), WOLF_INDEX, penetrations);
		const std::span<const fang::PenetrationSample> touching(penetrations, penetrationCount);

		wolf.position += fang::ResolvePenetration(touching);
		wolf.position += fang::SlideAlongNormals(fang::MakeMoveDelta(stick, 0.0f, MOVE_SPEED, DELTA_TIME), touching);

		const fang::ColliderProxy wolfProxy{
			.shape = fang::MakeColliderShape(
				fang::Capsule{
					.pointA = { wolf.position.x, wolf.position.y + WOLF_RADIUS, wolf.position.z },
					.pointB = { wolf.position.x, wolf.position.y + 80.0f, wolf.position.z },
					.radius = WOLF_RADIUS,
				}
			),
			.userIndex = WOLF_INDEX,
		};

		const fang::ColliderProxy proxies[] = { wolfProxy, wall };
		world.Update(proxies);
	};

	// まっすぐ壁へ 120 フレーム歩く。何もしなければ 800 cm 進むので、壁を通り抜けるには十分な量。
	for (int frame = 0; frame < 120; ++frame)
	{
		RunFrame(fang::Vector2{ 0.0f, 1.0f });
	}

	// 壁の手前で止まっている。カプセルの半径ぶん手前が接触面。
	CHECK(wolf.position.x <= WALL_X - WOLF_RADIUS + fang::PENETRATION_SKIN_CENTIMETERS + 0.5f);
	CHECK(wolf.position.x > WALL_X - WOLF_RADIUS - 5.0f);

	// 壁に貼り付いた状態で、最後の 20 フレームは動いていない（振動していない）。
	float previousX = wolf.position.x;
	for (int frame = 0; frame < 20; ++frame)
	{
		RunFrame(fang::Vector2{ 0.0f, 1.0f });
		CHECK(wolf.position.x == doctest::Approx(previousX));
		previousX = wolf.position.x;
	}

	// 斜めに押し当てると、壁に沿って横へ進む（引っかかって止まらない）。
	const float startZ = wolf.position.z;
	for (int frame = 0; frame < 60; ++frame)
	{
		RunFrame(fang::Vector2{ 1.0f, 1.0f });
	}

	CHECK(wolf.position.z < startZ - 100.0f);
	CHECK(wolf.position.x <= WALL_X - WOLF_RADIUS + fang::PENETRATION_SKIN_CENTIMETERS + 0.5f);

	world.Shutdown();
}
