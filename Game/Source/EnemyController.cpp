/**
 * @file EnemyController.cpp
 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
 */
#include "EnemyController.h"
#include "Collision/CollisionWorld.h"
#include "Core/Math/Matrix4x4.h"
#include "Resource/HeightmapTerrain.h"
#include "Scene/CharacterController.h"
#include "MeleeDamage.h"


namespace fang::game
{
	EnemyController::EnemyController(const Dependencies& dependencies, const Vector3& initialPosition)
		: m_dependencies(dependencies)
		, m_position(initialPosition)
	{
	}


	void EnemyController::Update(float deltaTimeSeconds, ActorHandle self, Scene& scene)
	{
		//------------------------------------------------------------------------
		// 1. 相手の位置と生死を読む
		//------------------------------------------------------------------------
		Vector3    targetPosition;
		const bool hasTarget = m_dependencies.targetHandle != nullptr && scene.IsValid(*m_dependencies.targetHandle);
		if (hasTarget)
		{
			const Matrix4x4 targetWorld = scene.GetWorldMatrix(*m_dependencies.targetHandle);
			targetPosition              = Vector3{ targetWorld.m[3][0], targetWorld.m[3][1], targetWorld.m[3][2] };
		}

		//------------------------------------------------------------------------
		// 2. センサー ➡ ブラックボード
		//------------------------------------------------------------------------
		PerceptionResult perception;
		if (hasTarget && m_dependencies.collisionWorld != nullptr)
		{
			const PerceptionInput input{
				.selfPosition      = m_position,
				.selfFacingRadians = m_facingRadians,
				.targetPosition    = targetPosition,
				.selfUserIndex     = self.index,
				.targetUserIndex   = m_dependencies.targetHandle->index,
			};
			perception = Sense(*m_dependencies.collisionWorld, m_dependencies.parameter->perception, input);
		}

		WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

		//------------------------------------------------------------------------
		// 3. 振り。移動より前に置く（狼と同じ理由。m_position も掃引が見る登録も前フレームのもの、ADR-034）。
		// 　合図は「見えていて、牙の間合いの内」。間合いに一度も入っていなければ掃引は 0 本のまま。
		//------------------------------------------------------------------------
		if (m_dependencies.collisionWorld != nullptr)
		{
			const MeleeSwingParameter& swingParameter = m_dependencies.parameter->swing;

			const MeleeSwingInput swingInput{
				.selfPosition        = m_position,
				.selfFacingRadians   = m_facingRadians,
				.isAttackRequested   = m_blackboard.isTargetVisible &&
									   m_blackboard.distanceToTargetCentimeters <= swingParameter.reachCentimeters,
				.selfUserIndex       = self.index,
				.targetAttributeMask = COLLISION_ATTRIBUTE_WOLF,
			};

			SweepHit               hits[MAX_MELEE_SWING_HIT_COUNT];
			const MeleeSwingResult swingResult = StepMeleeSwing(
				*m_dependencies.collisionWorld,
				swingParameter,
				swingInput,
				deltaTimeSeconds,
				&m_swingState,
				hits
			);

			ApplyMeleeHits(scene, std::span<const SweepHit>(hits, swingResult.newHitCount), swingParameter.attackPower);
		}

		//------------------------------------------------------------------------
		// 4. 意思決定。振りの最中は答えを捨てる（進む量も向きも変えない。踏み込みの空振りを許す）。
		//------------------------------------------------------------------------
		MoveIntent intent =
			StepPursuit(m_dependencies.parameter->pursuit, m_blackboard, m_position, deltaTimeSeconds, &m_state);

		if (IsMeleeSwingInProgress(m_swingState))
		{
			intent = MoveIntent{};
		}

		//------------------------------------------------------------------------
		// 5. エフェクター（移動・向き・接地）。押し出しは振りの最中も効かせる（自分から進む量だけ止める）。
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
				m_dependencies.parameter->pursuit.turnSpeedRadiansPerSecond * deltaTimeSeconds
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
