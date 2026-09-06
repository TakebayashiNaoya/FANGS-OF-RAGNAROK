/**
 * @file CameraOcclusion.h
 * @brief 遮蔽物との間に視点を挟まないよう、カメラを注視点へ寄せる解決。
 * @details 状態を持たない自由関数と POD だけを置く（MeleeSwing / Perception と同じ性格のもの）。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"


namespace fang
{
	class CollisionWorld;

	/**
	 * @brief 遮られたときにカメラを注視点へ寄せる調整値。
	 * @details FANG_REFLECT 付きの POD。方位・俯角・注視点は持たない（この解決が動かさないものなので）。
	 */
	struct CameraOcclusionParameter
	{
		FANG_REFLECT_BEGIN(CameraOcclusionParameter)
		FANG_FIELD(minimumDistanceCentimeters, "寄せる下限", Range(0.0f, 2000.0f))
		FANG_FIELD(returnSpeedCentimetersPerSecond, "戻す速さ", Range(0.0f, 4000.0f))
		FANG_FIELD(sweepRadiusCentimeters, "掃引の半径", Range(0.0f, 200.0f))
		FANG_REFLECT_END()

		/** @brief これ以上は寄せない距離。俯角 20 度で狼の頂点境界の角まで 29.9cm 残る値(要件)。 */
		float minimumDistanceCentimeters = 150.0f;

		/** @brief 遮蔽が外れた後に伸ばす速さの上限。寄せ側には掛けない。 */
		float returnSpeedCentimetersPerSecond = 400.0f;

		/** @brief 掃引する球の半径。近平面の隅(画角 60 度・16:9 で 15.45cm)を包む値。 */
		float sweepRadiusCentimeters = 20.0f;

		/** @brief 遮蔽と数える種別のビット。意味は Game が決める。調整つまみではないので反映しない。 */
		uint32_t blockerAttributeMask = ALL_COLLISION_ATTRIBUTE_MASK;
	};

	/** @brief 解くための、その瞬間の値。 */
	struct CameraOcclusionInput
	{
		Vector3 targetPosition;     /**< 注視点。 */
		Vector3 defaultEyePosition; /**< 遮られていないときの視点。方位・俯角・既定の距離は呼び出し側で決め切る。 */

		/** @brief 前フレームが返した距離。0 以下なら「持ち越しが無い」印で、戻しの速度制限を掛けない。 */
		float previousDistanceCentimeters = 0.0f;

		float deltaTimeSeconds = 0.0f;
	};

	/** @brief 解いた結果。 */
	struct CameraOcclusionResult
	{
		/** @brief 注視点と既定の視点を結ぶ線分の上の点。遮られていなければ既定の視点そのもの。 */
		Vector3 eyePosition;

		/** @brief 注視点からの距離。次のフレームの previousDistanceCentimeters にそのまま渡す。 */
		float distanceCentimeters = 0.0f;

		bool didHitBlocker = false; /**< 掃引が遮蔽物に触れたか。デバッグ表示とテスト用。 */
	};

	/**
	 * @brief 遮蔽物に当たらない位置まで視点を注視点へ寄せる。
	 * @param world nullptr でよい。その場合は掃引せず既定の視点をそのまま返す。
	 * @details 掃引は 1 回だけ。寄せは即時、戻しだけ速度制限(この非対称が振動止め)。
	 *          ヒープ確保をしない ➡ 更新ジョブから毎フレーム呼んでよい。
	 * @threading 更新ジョブ 1 本から。CollisionWorld::Update の後に呼ぶこと(今フレームの世界を見るため)。
	 */
	[[nodiscard]] CameraOcclusionResult SolveCameraOcclusion(
		const CollisionWorld*           world,
		const CameraOcclusionParameter& parameter,
		const CameraOcclusionInput&     input
	);
} // namespace fang
