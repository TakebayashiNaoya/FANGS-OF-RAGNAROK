/**
 * @file WolfController.cpp
 * @brief 狼 1 体ぶんの移動・接地・アニメーションを進める振る舞い。
 */
#include "WolfController.h"
#include "Animation/AnimationPlayback.h"
#include "Animation/SkeletalAnimation.h"
#include "Collision/CollisionWorld.h"
#include "Core/Log/Assert.h"
#include "Core/Math/Matrix4x4.h"
#include "CollisionAttribute.h"
#include "MeleeDamage.h"


namespace fang::game
{
	WolfController::WolfController(
		const Dependencies&     dependencies,
		CollisionWorld*         collisionWorld,
		const HeightmapTerrain* terrain,
		const Vector3&          initialPosition,
		float                   initialFacingRadians
	)
		: CharacterBase(
			  GroundDependencies{ .collisionWorld = collisionWorld, .terrain = terrain },
			  initialPosition,
			  initialFacingRadians
		  )
		, m_dependencies(dependencies)
	{
	}


	void WolfController::SetControlled(bool isControlled)
	{
		m_isControlled = isControlled;
		if (isControlled)
		{
			// 引き継いだ直後、押しっぱなしのボタンで振り出さないよう「既に押されていた」ことにしておく。
			m_swingState.wasAttackRequested = true;
		}
	}


	void WolfController::SetFrameInput(const GamepadState& gamepad, float cameraYawRadians)
	{
		m_moveStick         = GetLeftStick(gamepad);
		m_cameraYawRadians  = cameraYawRadians;
		m_isAttackRequested = IsButtonDown(gamepad, EnGamepadButton::X);
	}


	void WolfController::Update(float deltaTimeSeconds, Actor self)
	{
		// 1. 振り(操作する狼のみ)。移動より前に置く ➡ position はまだ前フレームに書いた位置のまま。
		//    掃引が見る登録も前フレームのもの(ADR-034) ➡ 牙と相手が同じ瞬間の世界で揃う。
		if (m_isControlled && GetCollisionWorld() != nullptr)
		{
			const MeleeSwingInput swingInput{
				.selfPosition        = GetPosition(),
				.selfFacingRadians   = GetFacingRadians(),
				.isAttackRequested   = m_isAttackRequested,
				.selfUserIndex       = self.GetIndex(),
				.targetAttributeMask = COLLISION_ATTRIBUTE_ENEMY,
			};

			SweepHit               hits[MAX_MELEE_SWING_HIT_COUNT];
			const MeleeSwingResult swingResult = StepMeleeSwing(
				*GetCollisionWorld(),
				*m_dependencies.swingParameter,
				swingInput,
				deltaTimeSeconds,
				&m_swingState,
				hits
			);

			ApplyMeleeHits(
				self,
				std::span<const SweepHit>(hits, swingResult.newHitCount),
				m_dependencies.swingParameter->attackPower
			);
		}

		float appliedSpeed = 0.0f;

		if (m_isControlled)
		{
			const Vector3 desiredDelta = MakeMoveDelta(
				m_moveStick,
				m_cameraYawRadians,
				m_dependencies.parameter->moveSpeedCentimetersPerSecond,
				deltaTimeSeconds
			);

			// 前フレームの接触から押し出しつつ、進みたい量を壁に沿わせて足す。当たり判定が無ければ接触なし。
			const Vector3 appliedDelta = MovePosition(desiredDelta, self.GetIndex());

			appliedSpeed = Length(appliedDelta) / (deltaTimeSeconds > 0.0f ? deltaTimeSeconds : 1.0f);
			if (LengthSquared(appliedDelta) > 0.0f)
			{
				TurnFacingTowards(
					GetYawFromDirection(appliedDelta),
					m_dependencies.parameter->turnSpeedRadiansPerSecond * deltaTimeSeconds
				);
			}
		}

		// 足裏はローカル y = 0 ➡ 地表の高さを Y へ足すだけで接地する。GetHeightAt は範囲外を端の高さへ
		// クランプするので、狼が地形の外へ出ても高さが未定義にならない。
		WriteTransform(self);

		if (m_dependencies.isSkinned && m_dependencies.animation != nullptr && m_dependencies.animation->IsReady())
		{
			if (m_isControlled)
			{
				// 速さに合わせて進める ➡ 止まれば姿勢も止まり、ゆっくり倒せばゆっくり歩く。
				m_dependencies.playback->SetPlaybackSpeed(
					appliedSpeed / m_dependencies.parameter->moveSpeedCentimetersPerSecond
				);
				m_dependencies.playback->Advance(deltaTimeSeconds);

				FANG_VERIFY(m_dependencies.animation->ComputeSkinningMatrices(
					m_dependencies.playback->GetTimeRatio(),
					m_dependencies.inverseBindMatrices,
					m_dependencies.skinningMatricesStorage
				));
			}

			// 操作していない狼は再生を進めず、共有の置き場をそのまま指す ➡ 同じポーズで歩いて見える。
			(void)self.SetSkinningMatrices(m_dependencies.skinningMatricesStorage);
		}
	}
} // namespace fang::game
