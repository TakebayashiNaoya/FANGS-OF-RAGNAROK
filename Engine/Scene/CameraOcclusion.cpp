/**
 * @file CameraOcclusion.cpp
 * @brief 遮蔽物との間に視点を挟まないよう、カメラを注視点へ寄せる解決。
 */
#include "Pch.h"
#include "Scene/CameraOcclusion.h"
#include "Collision/CollisionWorld.h"
#include <algorithm>


namespace fang
{
	CameraOcclusionResult SolveCameraOcclusion(
		const CollisionWorld*           world,
		const CameraOcclusionParameter& parameter,
		const CameraOcclusionInput&     input
	)
	{
		const Vector3 targetToDefaultEye = input.defaultEyePosition - input.targetPosition;
		const float   defaultDistance    = Length(targetToDefaultEye);

		// 縮める余地が無い(既定が下限以下、注視点と視点が同じ位置)なら掃引しない。ゼロ除算もここで落ちる。
		if (defaultDistance <= parameter.minimumDistanceCentimeters)
		{
			return CameraOcclusionResult{ .eyePosition         = input.defaultEyePosition,
										  .distanceCentimeters = defaultDistance };
		}

		CameraOcclusionResult result;
		float                 solvedDistance = defaultDistance;

		if (world != nullptr)
		{
			SweepHit          nearestHit[1];
			const SweepResult sweep = world->SweepSphere(
				Sphere{ .center = input.targetPosition, .radius = parameter.sweepRadiusCentimeters },
				targetToDefaultEye,
				QueryFilter{ .attributeMask = parameter.blockerAttributeMask },
				nearestHit
			);

			if (sweep.hitCount > 0)
			{
				result.didHitBlocker = true;

				// timeRatio は始点 0・終点 1。始点で重なっていれば 0 で来るので、下限で止まる。
				solvedDistance = std::clamp(
					defaultDistance * nearestHit[0].timeRatio,
					parameter.minimumDistanceCentimeters,
					defaultDistance
				);
			}
		}

		// 寄せは即時。戻しだけ速度制限を掛ける。持ち越しが無い最初の 1 回は掛けない。
		if (input.previousDistanceCentimeters > 0.0f && solvedDistance > input.previousDistanceCentimeters)
		{
			solvedDistance = std::min(
				solvedDistance,
				input.previousDistanceCentimeters + parameter.returnSpeedCentimetersPerSecond * input.deltaTimeSeconds
			);
		}

		result.distanceCentimeters = solvedDistance;
		result.eyePosition         = input.targetPosition + targetToDefaultEye * (solvedDistance / defaultDistance);

		return result;
	}
} // namespace fang
