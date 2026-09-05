/**
 * @file MinionBehavior.cpp
 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
 */
#include "MinionBehavior.h"
#include "Collision/CollisionWorld.h"
#include "Core/Math/Matrix4x4.h"
#include "Resource/HeightmapTerrain.h"
#include "Scene/CharacterMovement.h"


namespace fang::game
{
	MinionBehavior::MinionBehavior(const Dependencies& dependencies, const Vector3& initialPosition)
		: m_dependencies(dependencies)
		, m_position(initialPosition)
	{
	}


	void MinionBehavior::Update(float deltaTimeSeconds, GameObjectHandle self, Scene& scene)
	{
		//------------------------------------------------------------------------
		// 1. 相手の位置と生死を読む
		//------------------------------------------------------------------------
		Vector3    targetPosition;
		const bool hasTarget = scene.IsValid(m_dependencies.targetHandle);
		if (hasTarget)
		{
			const Matrix4x4 targetWorld = scene.GetWorldMatrix(m_dependencies.targetHandle);
			targetPosition              = Vector3{ targetWorld.m[3][0], targetWorld.m[3][1], targetWorld.m[3][2] };
		}

		//------------------------------------------------------------------------
		// 2. センサー ➡ 3. ブラックボード
		//------------------------------------------------------------------------
		PerceptionResult perception;
		if (hasTarget && m_dependencies.collisionWorld != nullptr)
		{
			const PerceptionInput input{
				.selfPosition      = m_position,
				.selfFacingRadians = m_facingRadians,
				.targetPosition    = targetPosition,
				.selfUserIndex     = self.index,
				.targetUserIndex   = m_dependencies.targetHandle.index,
			};
			perception = Sense(*m_dependencies.collisionWorld, m_dependencies.params->perception, input);
		}

		WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

		//------------------------------------------------------------------------
		// 4. 意思決定
		//------------------------------------------------------------------------
		const MoveIntent intent =
			StepPursuit(m_dependencies.params->pursuit, m_blackboard, m_position, deltaTimeSeconds, &m_state);

		//------------------------------------------------------------------------
		// 5. エフェクター（移動・向き・接地）
		//------------------------------------------------------------------------
		const std::span<const Contact> contacts = (m_dependencies.collisionWorld != nullptr)
													  ? m_dependencies.collisionWorld->GetContacts()
													  : std::span<const Contact>{};

		const ContactMoveResult moveResult = MoveWithContacts(m_position, intent.desiredDelta, contacts, self.index);
		m_position                         = moveResult.position;

		if (intent.wantsToTurn)
		{
			m_facingRadians = TurnTowards(
				m_facingRadians,
				intent.facingRadians,
				m_dependencies.params->pursuit.turnSpeedRadiansPerSecond * deltaTimeSeconds
			);
		}

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

		//------------------------------------------------------------------------
		// 6. 見た目（狼と共有のスキニング行列）
		//------------------------------------------------------------------------
		if (!m_dependencies.skinningMatricesStorage.empty())
		{
			(void)scene.SetSkinningMatrices(self, m_dependencies.skinningMatricesStorage);
		}
	}
} // namespace fang::game
