/**
 * @file WolfBehavior.cpp
 * @brief 狼 1 体ぶんの移動・接地・アニメーションを進める振る舞い。
 */
#include "WolfBehavior.h"
#include "Animation/AnimationPlayback.h"
#include "Animation/SkeletalAnimation.h"
#include "Collision/CollisionWorld.h"
#include "Core/Log/Assert.h"
#include "Core/Math/Matrix4x4.h"
#include "Resource/HeightmapTerrain.h"
#include "Scene/CharacterMovement.h"


namespace fang::game
{
	WolfBehavior::WolfBehavior(
		bool                      isControlled,
		const WolfMovementParams& params,
		const Dependencies&       dependencies,
		const Vector3&            initialPosition,
		float                     initialFacingRadians
	)
		: m_isControlled(isControlled)
		, m_params(params)
		, m_dependencies(dependencies)
		, m_position(initialPosition)
		, m_facingRadians(initialFacingRadians)
	{
	}


	void WolfBehavior::SetFrameInput(const GamepadState& gamepad, float cameraYawRadians)
	{
		m_gamepad          = gamepad;
		m_cameraYawRadians = cameraYawRadians;
	}


	void WolfBehavior::Update(float deltaTimeSeconds, GameObjectHandle self, Scene& scene)
	{
		float appliedSpeed = 0.0f;

		if (m_isControlled)
		{
			// 前フレームの接触から押し出しつつ、進みたい量を壁に沿わせて足す。当たり判定が無ければ接触なし。
			const std::span<const Contact> contacts = (m_dependencies.collisionWorld != nullptr)
														  ? m_dependencies.collisionWorld->GetContacts()
														  : std::span<const Contact>{};

			const Vector3 desiredDelta = MakeMoveDelta(
				GetLeftStick(m_gamepad),
				m_cameraYawRadians,
				m_params.moveSpeedCentimetersPerSecond,
				deltaTimeSeconds
			);

			const ContactMoveResult moveResult = MoveWithContacts(m_position, desiredDelta, contacts, self.index);
			m_position                         = moveResult.position;

			appliedSpeed = Length(moveResult.appliedDelta) / (deltaTimeSeconds > 0.0f ? deltaTimeSeconds : 1.0f);
			if (LengthSquared(moveResult.appliedDelta) > 0.0f)
			{
				m_facingRadians = TurnTowards(
					m_facingRadians,
					GetYawFromDirection(moveResult.appliedDelta),
					m_params.turnSpeedRadiansPerSecond * deltaTimeSeconds
				);
			}
		}

		// 足裏はローカル y = 0 ➡ 地表の高さを Y へ足すだけで接地する。GetHeightAt は範囲外を端の高さへ
		// クランプするので、狼が地形の外へ出ても高さが未定義にならない。
		float groundHeight = 0.0f;
		if (m_dependencies.terrain != nullptr)
		{
			groundHeight = m_dependencies.terrain->GetHeightAt(m_position.x, m_position.z);
		}

		(void)scene.SetLocalTransform(
			self,
			Vector3{ m_position.x, m_position.y + groundHeight, m_position.z },
			m_facingRadians
		);

		if (m_dependencies.isSkinned && m_dependencies.animation != nullptr && m_dependencies.animation->IsReady())
		{
			if (m_isControlled)
			{
				// 速さに合わせて進める ➡ 止まれば姿勢も止まり、ゆっくり倒せばゆっくり歩く。
				m_dependencies.playback->SetPlaybackSpeed(appliedSpeed / m_params.moveSpeedCentimetersPerSecond);
				m_dependencies.playback->Advance(deltaTimeSeconds);

				FANG_VERIFY(m_dependencies.animation->ComputeSkinningMatrices(
					m_dependencies.playback->GetTimeRatio(),
					m_dependencies.inverseBindMatrices,
					m_dependencies.skinningMatricesStorage
				));
			}

			// 操作していない狼は再生を進めず、共有の置き場をそのまま指す ➡ 同じポーズで歩いて見える。
			(void)scene.SetSkinningMatrices(self, m_dependencies.skinningMatricesStorage);
		}
	}
} // namespace fang::game
