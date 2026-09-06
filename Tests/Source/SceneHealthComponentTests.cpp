/**
 * @file SceneHealthComponentTests.cpp
 * @brief HealthComponent と ApplyDamage / TickInvincibility、Scene の HP まわり（GetHandleFromIndex /
 *        IsPendingDestroy 含む）のテスト。ダメージ・無敵時間の境目・撃破・破棄反映後の取り外しを確かめる。
 */
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>


TEST_CASE("ApplyDamage: HPが攻撃力ぶん減り、0以下でwasDefeatedがtrueになる")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f };

	const fang::DamageResult first = fang::ApplyDamage(&health, 50.0f);
	CHECK(first.wasApplied);
	CHECK_FALSE(first.wasDefeated);
	CHECK(health.currentHitPoints == doctest::Approx(50.0f));

	const fang::DamageResult second = fang::ApplyDamage(&health, 50.0f);
	CHECK(second.wasApplied);
	CHECK(second.wasDefeated);
	CHECK(health.currentHitPoints == doctest::Approx(0.0f));
}


TEST_CASE("ApplyDamage: 攻撃力0では減らず、撃破もされない")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f };

	for (int hit = 0; hit < 1000; ++hit)
	{
		const fang::DamageResult result = fang::ApplyDamage(&health, 0.0f);
		CHECK(result.wasApplied);
		CHECK_FALSE(result.wasDefeated);
	}
	CHECK(health.currentHitPoints == doctest::Approx(100.0f));
}


TEST_CASE("ApplyDamage: HP0で湧いた相手は最初の当たりで撃破される")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 0.0f };
	CHECK(fang::ApplyDamage(&health, 50.0f).wasDefeated);
}


TEST_CASE("ApplyDamage: 無敵の残りが0より大きい間は入らない。0になったフレームから入る")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f, .invincibleSeconds = 0.5f };

	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(health.currentHitPoints == doctest::Approx(90.0f));

	// 刻み0.1秒で4フレーム目まではまだ無敵の残りがある(0.5 -> 0.1)。
	for (int frame = 0; frame < 4; ++frame)
	{
		fang::TickInvincibility(&health, 0.1f);
		CHECK_FALSE(fang::ApplyDamage(&health, 10.0f).wasApplied);
	}
	CHECK(health.currentHitPoints == doctest::Approx(90.0f));

	// 5フレーム目で無敵の残りが0になり、入るようになる。
	fang::TickInvincibility(&health, 0.1f);
	CHECK(health.invincibleSecondsRemaining == doctest::Approx(0.0f));
	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(health.currentHitPoints == doctest::Approx(80.0f));
}


TEST_CASE("ApplyDamage: 同じフレームに複数回当てても入るのは1回ぶん")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f, .invincibleSeconds = 0.5f };

	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK_FALSE(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK_FALSE(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(health.currentHitPoints == doctest::Approx(90.0f));
}


TEST_CASE("ApplyDamage: 無敵時間より長い間隔なら2回とも入る")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f, .invincibleSeconds = 0.5f };

	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	fang::TickInvincibility(&health, 0.6f);
	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(health.currentHitPoints == doctest::Approx(80.0f));
}


TEST_CASE("ApplyDamage: 無敵0なら毎回入る")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f, .invincibleSeconds = 0.0f };

	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(fang::ApplyDamage(&health, 10.0f).wasApplied);
	CHECK(health.currentHitPoints == doctest::Approx(70.0f));
}


TEST_CASE("SetMaximumHitPoints: 最大HPが増えた分だけ今のHPも増える(満タンにはしない)")
{
	fang::HealthComponent health{ .maximumHitPoints = 300.0f, .currentHitPoints = 100.0f };

	fang::SetMaximumHitPoints(&health, 345.0f);
	CHECK(health.maximumHitPoints == doctest::Approx(345.0f));
	CHECK(health.currentHitPoints == doctest::Approx(145.0f)); // 100 + (345 - 300)。満タンの345にはならない。
}


TEST_CASE("SetMaximumHitPoints: 下げ戻したときは今のHPを新しい上限で頭打ちにする")
{
	fang::HealthComponent health{ .maximumHitPoints = 345.0f, .currentHitPoints = 345.0f };

	fang::SetMaximumHitPoints(&health, 300.0f);
	CHECK(health.maximumHitPoints == doctest::Approx(300.0f));
	CHECK(health.currentHitPoints == doctest::Approx(300.0f));
}


TEST_CASE("SetMaximumHitPoints: 同じ値で呼んでも今のHPは変わらない(毎フレーム呼んでよい)")
{
	fang::HealthComponent health{ .maximumHitPoints = 300.0f, .currentHitPoints = 200.0f };

	fang::SetMaximumHitPoints(&health, 300.0f);
	CHECK(health.maximumHitPoints == doctest::Approx(300.0f));
	CHECK(health.currentHitPoints == doctest::Approx(200.0f));
}


TEST_CASE("Scene: HealthComponentは1個まで持て、破棄反映で取り外される")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle object = scene.CreateObject();

	CHECK(scene.GetHealthComponent(object) == nullptr);
	CHECK(scene.AddHealthComponent(object, fang::HealthComponent{}));
	CHECK(scene.GetHealthComponent(object) != nullptr);
	CHECK_FALSE(scene.AddHealthComponent(object, fang::HealthComponent{})); // 2 個目は拒否される。

	fang::HealthComponent* health = scene.GetHealthComponent(object);
	CHECK(fang::ApplyDamage(health, 200.0f).wasDefeated);

	// 破棄予約後は IsPendingDestroy が立つ ➡ ApplyMeleeHits 側がここで弾く（失敗ケース）。
	scene.DestroyObject(object);
	CHECK(scene.IsPendingDestroy(object));

	scene.Update(0.0f);

	CHECK_FALSE(scene.IsValid(object));

	// 破棄された分だけ HealthComponent も畳まれる（別のオブジェクトが持つ分に影響しない）。
	const fang::ActorHandle other = scene.CreateObject();
	CHECK(scene.AddHealthComponent(other, fang::HealthComponent{}));
	CHECK(scene.GetHealthComponent(other) != nullptr);

	scene.Shutdown();
}


TEST_CASE("Scene: GetHandleFromIndexとIsPendingDestroyで破棄予約済みの相手を弾ける")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle object = scene.CreateObject();
	CHECK(scene.AddHealthComponent(object, fang::HealthComponent{}));

	const fang::ActorHandle sameSeat = scene.GetHandleFromIndex(object.index);
	CHECK(sameSeat == object);
	CHECK_FALSE(scene.IsPendingDestroy(object));

	scene.DestroyObject(object);

	// 破棄反映は次の Update まで起きないので、IsValid はまだ true、IsPendingDestroy が代わりに立つ。
	CHECK(scene.IsValid(object));
	CHECK(scene.IsPendingDestroy(object));

	scene.Update(0.0f);

	// 席が空いた後は、古い世代のハンドルでは引けない。
	CHECK_FALSE(scene.GetHandleFromIndex(object.index) == object);
	CHECK_FALSE(scene.IsValid(object));

	scene.Shutdown();
}


TEST_CASE("Scene::Update: 無敵の残りを毎フレーム減らす")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle object = scene.CreateObject();
	CHECK(scene.AddHealthComponent(object, fang::HealthComponent{ .invincibleSeconds = 0.5f }));

	fang::HealthComponent* health = scene.GetHealthComponent(object);
	CHECK(fang::ApplyDamage(health, 10.0f).wasApplied);
	CHECK(health->invincibleSecondsRemaining == doctest::Approx(0.5f));

	scene.Update(0.5f);
	CHECK(scene.GetHealthComponent(object)->invincibleSecondsRemaining == doctest::Approx(0.0f));

	scene.Shutdown();
}
