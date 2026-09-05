/**
 * @file CollisionQuery.h
 * @brief クエリの型（QueryFilter・RayHit・SweepHit・SweepResult）と種別ビットの定数。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include <cstdint>
#include <span>


namespace fang
{
	/** @brief すべての種別。ビットの意味は Collision が決めず、使う側が割り当てる。 */
	inline constexpr uint32_t ALL_COLLISION_LAYERS = 0xFFFFFFFFu;

	/** @brief 1 回のクエリが Broadphase から受け取れる候補の上限。CollisionWorldDesc の既定と同じ数。 */
	inline constexpr uint32_t MAX_QUERY_CANDIDATE_COUNT = 1024;

	/**
	 * @brief クエリが見る相手を絞る条件。
	 * @details 既定のまま渡すと全部を見る ➡ 絞り込みを入れる前と同じ結果になる。
	 */
	struct QueryFilter
	{
		/** @brief 見る種別。登録の layerMask との AND が 0 でないものだけを見る。 */
		uint32_t layerMask = ALL_COLLISION_LAYERS;

		/** @brief 結果から外す userIndex。呼び出し側のスタックの配列でよい（クエリの間だけ読む）。 */
		std::span<const uint32_t> excludedUserIndices;
	};

	/** @brief レイキャストの結果。 */
	struct RayHit
	{
		uint32_t userIndex = 0; /**< 当たったコライダーの呼び出し側の番号。 */

		Vector3 point;  /**< ワールド空間の交点。 */
		Vector3 normal; /**< 当たった面の外向き。始点が形の中なら -direction。 */

		float distance = 0.0f; /**< 始点から交点までの距離。始点が形の中なら 0。 */
	};

	/** @brief 掃引が触れた登録 1 つ。 */
	struct SweepHit
	{
		uint32_t userIndex = 0;

		Vector3 point;  /**< 最初に触れた瞬間のワールド接触点。終点まで動かした位置ではない。 */
		Vector3 normal; /**< 触れた面の外向き。始点で既に重なっていれば押し出す向き。 */

		/** @brief 始点を 0、終点を 1 と見た、最初に触れた位置。始点で重なっていれば 0。 */
		float timeRatio = 0.0f;
	};

	/** @brief 掃引の結果。 */
	struct SweepResult
	{
		uint32_t hitCount = 0;

		/** @brief 書き込み先が足りず、遠いほうの当たりを捨てた。 */
		bool isTruncated = false;
	};
} // namespace fang
