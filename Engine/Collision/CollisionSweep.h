/**
 * @file CollisionSweep.h
 * @brief 保守的前進による掃引。Collision の内部用（傘ヘッダには入れない）。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Collision/CollisionShapes.h"
#include "Core/Math/Vector3.h"


namespace fang
{
	/** @brief 掃引の反復回数。正面から近づく配置は 2〜3 回で収まる。16 回で詰まらないのはほぼ接線方向にかすめる配置。 */
	inline constexpr int SWEEP_ITERATION_COUNT = 16;

	/** @brief 触れたとみなす分離距離の許容差。1 = 1cm なので 0.1mm。float の丸めより十分大きい。 */
	inline constexpr float SWEEP_TOUCH_TOLERANCE = 0.01f;

	/** @brief 2 つの形の離れ具合。触れていれば距離が負になる。 */
	struct Separation
	{
		Vector3 pointOnA;        /**< 芯どうしの最近点のうち A 側。接触点はここから半径ぶん進めた位置。 */
		Vector3 normal;          /**< A から B へ向く単位ベクトル。決められないときは +Y。 */
		float   distance = 0.0f; /**< 正なら表面どうしの隙間、負ならめり込みの深さ。 */
	};

	/**
	 * @brief 芯 + 半径の形（movingShape）と、登録された形（target）の離れ具合。
	 * @details 距離の式は Narrowphase・CollisionMath と同じもの。球は pointA == pointB の潰れたカプセル。
	 */
	void ComputeSeparation(const Capsule& movingShape, const ColliderShape& target, Separation* outSeparation);

	/**
	 * @brief 相手 1 つへの掃引。触れなければ false。
	 * @details 保守的前進。分離距離ぶん時刻を進めるのを SWEEP_ITERATION_COUNT 回まで繰り返す。
	 */
	[[nodiscard]] bool SweepAgainstShape(
		const Capsule&       movingShape,
		const Vector3&       motion,
		const ColliderShape& target,
		SweepHit*            outHit
	);
} // namespace fang
