/**
 * @file Collision.h
 * @brief Collision モジュールの入口（Broadphase / Narrowphase / Query）。
 * @details 公開ヘッダをまとめた傘ヘッダ。他モジュールの .cpp はこれ 1 本で足りる。
 *          ヘッダの中からは傘を使わず、個別の include か前方宣言にすること。
 */
#pragma once

#include "Collision/Broadphase.h"
#include "Collision/CollisionDebugLines.h"
#include "Collision/CollisionQuery.h"
#include "Collision/CollisionShapes.h"
#include "Collision/CollisionWorld.h"
#include "Collision/DynamicAabbTreeBroadphase.h"
#include "Collision/Narrowphase.h"
#include "Collision/SweepAndPruneBroadphase.h"
#include "Collision/UniformGridBroadphase.h"


namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetCollisionModuleName();
} // namespace fang
