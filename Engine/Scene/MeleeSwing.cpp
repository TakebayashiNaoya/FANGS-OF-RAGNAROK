/**
 * @file MeleeSwing.cpp
 * @brief 近接攻撃の振り 1 本ぶんの時間割と、判定区間の掃引。
 */
#include "Pch.h"
#include "Scene/MeleeSwing.h"
#include "Collision/CollisionWorld.h"
#include <algorithm>
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 位相の繰り上がりを頭打ちにする回数。位相の数（Ready を除く 3 つ）と同じにしておけば十分。 */
		constexpr int MAX_PHASE_ADVANCE_COUNT = 3;

		/** @brief 位相ごとの持ち時間。Ready は時間で進まない（ボタン待ち）ので 0。 */
		[[nodiscard]] float GetPhaseDurationSeconds(const MeleeSwingParams& params, EnMeleeSwingPhase phase)
		{
			switch (phase)
			{
				case EnMeleeSwingPhase::WindUp: return params.windUpSeconds;
				case EnMeleeSwingPhase::Active: return params.activeSeconds;
				case EnMeleeSwingPhase::Recovery: return params.recoverySeconds;
				default: return 0.0f;
			}
		}

		/** @brief 位相を 1 つ進める。判定区間に入った瞬間だけ、牙の始点を弧の始まりで埋め直す。 */
		void AdvancePhase(const MeleeSwingParams& params, const MeleeSwingInput& input, MeleeSwingState* state)
		{
			switch (state->phase)
			{
				case EnMeleeSwingPhase::WindUp:
					state->phase = EnMeleeSwingPhase::Active;
					state->previousFangPosition =
						ComputeFangPosition(params, input.selfPosition, input.selfFacingRadians, 0.0f);
					break;

				case EnMeleeSwingPhase::Active: state->phase = EnMeleeSwingPhase::Recovery; break;

				case EnMeleeSwingPhase::Recovery: state->phase = EnMeleeSwingPhase::Ready; break;

				default: break;
			}
		}

		/** @brief この振りで既に当てた席番号か。 */
		[[nodiscard]] bool HasAlreadyHit(const MeleeSwingState& state, uint32_t userIndex)
		{
			for (uint8_t index = 0; index < state.hitCount; ++index)
			{
				if (state.hitUserIndices[index] == userIndex)
				{
					return true;
				}
			}
			return false;
		}
	} // namespace


	Vector3 ComputeFangPosition(
		const MeleeSwingParams& params,
		const Vector3&          selfPosition,
		float                   selfFacingRadians,
		float                   activeRatio
	)
	{
		const float fangAngleRadians = selfFacingRadians - params.arcRadians * 0.5f + params.arcRadians * activeRatio;

		const Vector3 planarOffset{
			std::cos(fangAngleRadians) * params.reachCentimeters,
			0.0f,
			std::sin(fangAngleRadians) * params.reachCentimeters,
		};

		return selfPosition + planarOffset + Vector3{ 0.0f, params.fangHeightCentimeters, 0.0f };
	}


	MeleeSwingResult StepMeleeSwing(
		const CollisionWorld&   world,
		const MeleeSwingParams& params,
		const MeleeSwingInput&  input,
		float                   deltaTimeSeconds,
		MeleeSwingState*        state,
		std::span<SweepHit>     outHits
	)
	{
		MeleeSwingResult result;

		const bool isRisingEdge    = input.isAttackButtonDown && !state->wasAttackButtonDown;
		state->wasAttackButtonDown = input.isAttackButtonDown;

		if (state->phase == EnMeleeSwingPhase::Ready)
		{
			if (!isRisingEdge)
			{
				return result;
			}

			state->phase          = EnMeleeSwingPhase::WindUp;
			state->elapsedSeconds = 0.0f;
			state->hitCount       = 0;
			result.didStartSwing  = true;
		}
		else
		{
			state->elapsedSeconds += deltaTimeSeconds;
		}

		// 秒数を使い切った位相はそのフレームのうちに次へ繰り上げる。回数を頭打ちにして、
		// どれかの区間が 0 秒でも無限ループしないようにする。
		for (int advance = 0; advance < MAX_PHASE_ADVANCE_COUNT; ++advance)
		{
			const float phaseDurationSeconds = GetPhaseDurationSeconds(params, state->phase);
			if (state->elapsedSeconds < phaseDurationSeconds)
			{
				break;
			}

			state->elapsedSeconds -= phaseDurationSeconds;
			AdvancePhase(params, input, state);
		}

		if (state->phase != EnMeleeSwingPhase::Active)
		{
			return result;
		}

		const float activeRatio =
			(params.activeSeconds > 0.0f) ? std::min(state->elapsedSeconds / params.activeSeconds, 1.0f) : 1.0f;
		const Vector3 fangPosition =
			ComputeFangPosition(params, input.selfPosition, input.selfFacingRadians, activeRatio);
		const Vector3 motion = fangPosition - state->previousFangPosition;

		const Sphere fangSphere{ .center = state->previousFangPosition, .radius = params.fangRadiusCentimeters };

		const uint32_t    excludedUserIndex = input.selfUserIndex;
		const QueryFilter filter{
			.layerMask           = input.targetLayerMask,
			.excludedUserIndices = std::span<const uint32_t>(&excludedUserIndex, 1),
		};

		SweepHit          sweepHits[MAX_MELEE_SWING_HIT_COUNT];
		const SweepResult sweepResult = world.SweepSphere(fangSphere, motion, filter, sweepHits);

		result.didSweep = true;
		if (sweepResult.isTruncated)
		{
			result.isTruncated = true;
		}

		state->previousFangPosition = fangPosition;

		// 掃引が近い順に返した中から、この振りでまだ当てていないものだけを詰め直す。
		// 記録が満杯なら「既に当てた」と同じ扱いにして落とす（多く当たる方向には壊れない）。
		uint32_t writtenCount = 0;
		for (uint32_t hitIndex = 0; hitIndex < sweepResult.hitCount; ++hitIndex)
		{
			const SweepHit& hit = sweepHits[hitIndex];
			if (HasAlreadyHit(*state, hit.userIndex))
			{
				continue;
			}

			if (state->hitCount >= MAX_MELEE_SWING_HIT_COUNT || writtenCount >= outHits.size())
			{
				result.isTruncated = true;
				continue;
			}

			state->hitUserIndices[state->hitCount] = hit.userIndex;
			++state->hitCount;

			outHits[writtenCount] = hit;
			++writtenCount;
		}

		result.newHitCount = writtenCount;
		return result;
	}
} // namespace fang
