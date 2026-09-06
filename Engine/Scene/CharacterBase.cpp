/**
 * @file CharacterBase.cpp
 * @brief 地表に立ち、壁に押し戻される 1 体ぶんの土台。
 */
#include "Pch.h"
#include "Scene/CharacterBase.h"
#include "Collision/CollisionWorld.h"
#include "Resource/HeightmapTerrain.h"


namespace fang
{
	CharacterBase::CharacterBase(
		const GroundDependencies& dependencies,
		const Vector3&            initialPosition,
		float                     initialFacingRadians
	)
		: m_collisionWorld(dependencies.collisionWorld)
		, m_terrain(dependencies.terrain)
	{
		m_state.position      = initialPosition;
		m_state.facingRadians = initialFacingRadians;
	}


	Vector3 CharacterBase::MovePosition(const Vector3& desiredDelta, uint32_t selfUserIndex)
	{
		const std::span<const Contact> contacts =
			(m_collisionWorld != nullptr) ? m_collisionWorld->GetContacts() : std::span<const Contact>{};

		const ContactMoveResult moveResult = MoveWithContacts(m_state.position, desiredDelta, contacts, selfUserIndex);
		m_state.position                   = moveResult.position;

		return moveResult.appliedDelta;
	}


	void CharacterBase::TurnFacingTowards(float targetRadians, float maxStepRadians)
	{
		m_state.facingRadians = TurnTowards(m_state.facingRadians, targetRadians, maxStepRadians);
	}


	void CharacterBase::WriteTransform(Actor self) const
	{
		float groundHeight = 0.0f;
		if (m_terrain != nullptr)
		{
			groundHeight = m_terrain->GetHeightAt(m_state.position.x, m_state.position.z);
		}

		(void)self.SetTransform(
			Vector3{ m_state.position.x, m_state.position.y + groundHeight, m_state.position.z },
			m_state.facingRadians
		);
	}
} // namespace fang
