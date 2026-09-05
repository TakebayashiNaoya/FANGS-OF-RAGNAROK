/**
 * @file SceneHealthComponentTests.cpp
 * @brief HealthComponent と ApplyDamage、Scene の HP まわり 4 本（GetHandleFromIndex / IsPendingDestroy 含む）
 *        のテスト。ダメージ・撃破・破棄反映後の取り外しを確かめる。
 */
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>


TEST_CASE("ApplyDamage: HPが攻撃力ぶん減り、0以下でtrueを返す")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f };

	CHECK_FALSE(fang::ApplyDamage(&health, 50.0f));
	CHECK(health.currentHitPoints == doctest::Approx(50.0f));

	CHECK(fang::ApplyDamage(&health, 50.0f));
	CHECK(health.currentHitPoints == doctest::Approx(0.0f));
}


TEST_CASE("ApplyDamage: 攻撃力0では減らず、撃破もされない")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 100.0f };

	for (int hit = 0; hit < 1000; ++hit)
	{
		CHECK_FALSE(fang::ApplyDamage(&health, 0.0f));
	}
	CHECK(health.currentHitPoints == doctest::Approx(100.0f));
}


TEST_CASE("ApplyDamage: HP0で湧いた相手は最初の当たりで撃破される")
{
	fang::HealthComponent health{ .maximumHitPoints = 100.0f, .currentHitPoints = 0.0f };
	CHECK(fang::ApplyDamage(&health, 50.0f));
}


TEST_CASE("Scene: HealthComponentは1個まで持て、破棄反映で取り外される")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::GameObjectHandle object = scene.CreateObject();

	CHECK(scene.GetHealthComponent(object) == nullptr);
	CHECK(scene.AddHealthComponent(object, fang::HealthComponent{}));
	CHECK(scene.GetHealthComponent(object) != nullptr);
	CHECK_FALSE(scene.AddHealthComponent(object, fang::HealthComponent{})); // 2 個目は拒否される。

	fang::HealthComponent* health = scene.GetHealthComponent(object);
	CHECK(fang::ApplyDamage(health, 200.0f));
	scene.DestroyObject(object);
	scene.Update(0.0f);

	CHECK_FALSE(scene.IsValid(object));

	// 破棄された分だけ HealthComponent も畳まれる（別のオブジェクトが持つ分に影響しない）。
	const fang::GameObjectHandle other = scene.CreateObject();
	CHECK(scene.AddHealthComponent(other, fang::HealthComponent{}));
	CHECK(scene.GetHealthComponent(other) != nullptr);

	scene.Shutdown();
}


TEST_CASE("Scene: GetHandleFromIndexとIsPendingDestroyで破棄予約済みの相手を弾ける")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::GameObjectHandle object = scene.CreateObject();
	CHECK(scene.AddHealthComponent(object, fang::HealthComponent{}));

	const fang::GameObjectHandle sameSeat = scene.GetHandleFromIndex(object.index);
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
