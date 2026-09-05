/**
 * @file Collision.cpp
 * @brief Collision モジュールの識別とログカテゴリの定義。
 */
#include "Pch.h"
#include "Collision/Collision.h"
#include "Collision/CollisionLog.h"


FANG_DEFINE_LOG_CATEGORY(Collision);


namespace fang
{
	const char* GetCollisionModuleName()
	{
		return "Collision";
	}
} // namespace fang
