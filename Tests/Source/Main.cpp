/**
 * @file Main.cpp
 * @brief doctest のエントリポイントとモジュール疎通のテスト。
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "Animation/Animation.h"
#include "Collision/Collision.h"
#include "Core/Core.h"
#include "Scene/Scene.h"
#include <doctest.h>
#include <cstring>


TEST_CASE("参照しているモジュールがリンクされている")
{
	CHECK(std::strcmp(fang::GetCoreModuleName(), "Core") == 0);
	CHECK(std::strcmp(fang::GetCollisionModuleName(), "Collision") == 0);
	CHECK(std::strcmp(fang::GetAnimationModuleName(), "Animation") == 0);
	CHECK(std::strcmp(fang::GetSceneModuleName(), "Scene") == 0);
}
