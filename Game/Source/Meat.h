/**
 * @file Meat.h
 * @brief 肉 1 個ぶんの Scene オブジェクトの生成。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Scene/Actor.h"


namespace fang
{
	class Scene;
} // namespace fang


namespace fang::game
{
	struct WolfModel;

	/**
	 * @brief 読み込み済みの WolfModel を縮めて、肉 1 個の Scene オブジェクトを作る。
	 * @details 見た目だけのオブジェクト。コライダーも HP も振る舞いも持たない
	 *          ➡ 狼が押し出されず、雑魚の追跡にも牙の掃引にも混ざらない（回収は距離で判定する）。
	 * @return 席が尽きていれば無効な Actor。
	 */
	[[nodiscard]] Actor CreateMeatObject(Scene& scene, const WolfModel& model, const Vector3& position);
} // namespace fang::game
