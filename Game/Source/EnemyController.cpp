/**
 * @file EnemyController.cpp
 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
 */
#include "EnemyController.h"
#include "Collision/CollisionWorld.h"
#include "Core/Math/Matrix4x4.h"
#include "MeleeDamage.h"


namespace fang::game
{
	EnemyController::EnemyController(
		const Dependencies&     dependencies,
		CollisionWorld*         collisionWorld,
		const HeightmapTerrain* terrain,
		const Vector3&          initialPosition
	)
		: CharacterBase(
			  GroundDependencies{ .collisionWorld = collisionWorld, .terrain = terrain },
			  initialPosition,
			  0.0f
		  )
		, m_dependencies(dependencies)
	{
	}


	void EnemyController::Update(float deltaTimeSeconds, Actor self)
	{
		//------------------------------------------------------------------------
		// 1. 相手の位置と生死を読む
		//------------------------------------------------------------------------
		Vector3     targetPosition;
		const Actor target =
			(m_dependencies.targetHandle != nullptr) ? self.GetActorFromHandle(*m_dependencies.targetHandle) : Actor{};
		const bool hasTarget = target.IsValid();
		if (hasTarget)
		{
			targetPosition = target.GetWorldPosition();
		}

		//------------------------------------------------------------------------
		// 2. センサー ➡ ブラックボード
		//------------------------------------------------------------------------
		PerceptionResult perception;
		if (hasTarget && GetCollisionWorld() != nullptr)
		{
			const PerceptionInput input{
				.selfPosition      = GetPosition(),
				.selfFacingRadians = GetFacingRadians(),
				.targetPosition    = targetPosition,
				.selfUserIndex     = self.GetIndex(),
				.targetUserIndex   = target.GetIndex(),
			};
			perception = Sense(*GetCollisionWorld(), m_dependencies.parameter->perception, input);
		}

		WritePerception(perception, targetPosition, deltaTimeSeconds, &m_blackboard);

		//------------------------------------------------------------------------
		// 3. 振り。移動より前に置く（狼と同じ理由。position も掃引が見る登録も前フレームのもの、ADR-034）。
		// 　合図は「見えていて、牙の間合いの内」。間合いに一度も入っていなければ掃引は 0 本のまま。
		//------------------------------------------------------------------------
		if (GetCollisionWorld() != nullptr)
		{
			const MeleeSwingParameter& swingParameter = m_dependencies.parameter->swing;

			const MeleeSwingInput swingInput{
				.selfPosition        = GetPosition(),
				.selfFacingRadians   = GetFacingRadians(),
				.isAttackRequested   = m_blackboard.isTargetVisible &&
									   m_blackboard.distanceToTargetCentimeters <= swingParameter.reachCentimeters,
				.selfUserIndex       = self.GetIndex(),
				.targetAttributeMask = COLLISION_ATTRIBUTE_WOLF,
			};

			SweepHit               hits[MAX_MELEE_SWING_HIT_COUNT];
			const MeleeSwingResult swingResult =
				StepMeleeSwing(*GetCollisionWorld(), swingParameter, swingInput, deltaTimeSeconds, &m_swingState, hits);

			ApplyMeleeHits(self, std::span<const SweepHit>(hits, swingResult.newHitCount), swingParameter.attackPower);
		}

		//------------------------------------------------------------------------
		// 4. 意思決定。振りの最中は答えを捨てる（進む量も向きも変えない。踏み込みの空振りを許す）。
		//------------------------------------------------------------------------
		MoveIntent intent =
			StepPursuit(m_dependencies.parameter->pursuit, m_blackboard, GetPosition(), deltaTimeSeconds, &m_state);

		if (IsMeleeSwingInProgress(m_swingState))
		{
			intent = MoveIntent{};
		}

		//------------------------------------------------------------------------
		// 5. エフェクター（移動・向き・接地）。押し出しは振りの最中も効かせる（自分から進む量だけ止める）。
		//------------------------------------------------------------------------
		MovePosition(intent.desiredDelta, self.GetIndex());

		if (intent.wantsToTurn)
		{
			TurnFacingTowards(
				intent.facingRadians,
				m_dependencies.parameter->pursuit.turnSpeedRadiansPerSecond * deltaTimeSeconds
			);
		}

		WriteTransform(self);

		//------------------------------------------------------------------------
		// 6. 見た目（狼と共有のスキニング行列）
		//------------------------------------------------------------------------
		if (!m_dependencies.skinningMatricesStorage.empty())
		{
			(void)self.SetSkinningMatrices(m_dependencies.skinningMatricesStorage);
		}
	}
} // namespace fang::game
