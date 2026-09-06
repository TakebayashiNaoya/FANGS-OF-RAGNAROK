/**
 * @file Meat.h
 * @brief 肉 1 個ぶんの Scene オブジェクトの生成。
 */
#pragma once

#include "Core/Math/Aabb.h"
#include "Renderer/MeshRenderer.h"
#include "Scene/Actor.h"


namespace fang
{
	class Scene;
} // namespace fang


namespace fang::game
{
	/**
	 * @brief 肉 1 個の Scene オブジェクトを作る。見た目はステージから借りた静的メッシュ 1 つ。
	 * @param mesh        借りるメッシュ。無効なら Transform だけのオブジェクトになる(寿命と回収は同じに動く)。
	 * @param localBounds mesh のモデル空間の箱。無効なメッシュのときは読まれない。
	 * @details ベースカラーも法線マップも渡さない ➡ MeshRenderer のダミー(sRGB 199, 194, 184 の単色・平坦法線)が差さる。
	 *          材質係数は既定(メタリック 0 / ラフネス 1)。コライダーも HP も振る舞いも持たない。
	 *          行列はここでは書かない。書くのは MeatManager の姿勢の段 1 人だけ(ADR-041)。
	 * @return 席が尽きていれば無効な Actor。
	 */
	[[nodiscard]] Actor CreateMeatObject(Scene& scene, MeshId mesh, const Aabb& localBounds);
} // namespace fang::game
