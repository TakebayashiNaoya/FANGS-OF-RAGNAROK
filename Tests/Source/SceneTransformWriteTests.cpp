/**
 * @file SceneTransformWriteTests.cpp
 * @brief Transform の書き手の重複検出のテスト。1 人なら 0 件、2 回書くと 1 件、無効ハンドルは汚さない、
 *        窓が 1 フレームで閉じる。Debug / Preview だけでコンパイルされる（FANG_ENABLE_SCENE_VALIDATION）。
 */
#include "Core/Math/Matrix4x4.h"
#include "Core/Memory/Allocator.h"
#include "Scene/Scene.h"
#include <doctest.h>


#if FANG_ENABLE_SCENE_VALIDATION

TEST_CASE("SceneTransformWrite: 1人だけが書けば重複検出は0件")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle handle = scene.CreateObject();
	CHECK(handle.IsValid());

	CHECK(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	CHECK(scene.GetTransformWriteCount(handle) == 1);

	scene.Update(1.0f / 60.0f);

	CHECK(scene.GetDuplicateTransformWriteCount() == 0);

	scene.Shutdown();
}


TEST_CASE("SceneTransformWrite: 同じフレームに2回書くと重複検出が1件、回数は2")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle handle = scene.CreateObject();
	CHECK(handle.IsValid());

	CHECK(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	CHECK(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	CHECK(scene.GetTransformWriteCount(handle) == 2);

	scene.Update(1.0f / 60.0f);

	CHECK(scene.GetDuplicateTransformWriteCount() == 1);

	scene.Shutdown();
}


TEST_CASE("SceneTransformWrite: 破棄済み・無効なハンドルへの書き込みは記録を汚さない")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle handle = scene.CreateObject();
	CHECK(handle.IsValid());
	scene.DestroyObject(handle);
	scene.Update(1.0f / 60.0f); // 破棄を反映し、世代を進める。

	// 世代が進んでいるので、もう無効なハンドル。
	CHECK_FALSE(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	CHECK(scene.GetTransformWriteCount(handle) == 0);

	scene.Update(1.0f / 60.0f);
	CHECK(scene.GetDuplicateTransformWriteCount() == 0);

	scene.Shutdown();
}


TEST_CASE("SceneTransformWrite: 窓は1フレームで閉じる（次のフレームには持ち越さない）")
{
	fang::Scene scene;
	CHECK(scene.Initialize(fang::HeapAllocator::GetInstance(), fang::SceneDesc{}));

	const fang::ActorHandle handle = scene.CreateObject();
	CHECK(handle.IsValid());

	CHECK(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	CHECK(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	scene.Update(1.0f / 60.0f);
	CHECK(scene.GetDuplicateTransformWriteCount() == 1);

	// 次のフレームは1回しか書かない ➡ 前の周の重複を持ち越さない。
	// GetTransformWriteCount は Update の末尾で 0 に戻る前の値を見るため、Update より前に読む。
	CHECK(scene.SetLocalMatrix(handle, fang::Matrix4x4{}));
	CHECK(scene.GetTransformWriteCount(handle) == 1);

	scene.Update(1.0f / 60.0f);
	CHECK(scene.GetDuplicateTransformWriteCount() == 0);

	scene.Shutdown();
}

#endif
