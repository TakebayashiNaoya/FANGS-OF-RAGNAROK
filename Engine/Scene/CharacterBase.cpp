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
		FANG_ASSERT(
			initialPosition.y == 0.0f,
			"足元の y は接地の前で 0 のこと。地表の高さは WriteTransform が足す（ADR-061）"
		);

		m_state.position      = initialPosition;
		m_state.facingRadians = initialFacingRadians;
	}


	Vector3 CharacterBase::MovePosition(const Vector3& desiredDelta, uint32_t selfUserIndex)
	{
		FANG_ASSERT(desiredDelta.y == 0.0f, "進みたい量は水平のこと（ADR-061）");

		const std::span<const Contact> contacts =
			(m_collisionWorld != nullptr) ? m_collisionWorld->GetContacts() : std::span<const Contact>{};

		const ContactMoveResult moveResult = MoveWithContacts(m_state.position, desiredDelta, contacts, selfUserIndex);
		m_state.position                   = moveResult.position;

		FANG_ASSERT(m_state.position.y == 0.0f, "押し戻しが縦を書いた。縦は接地の持ち物（ADR-061）");

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
