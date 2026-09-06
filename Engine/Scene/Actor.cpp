/**
 * @file Actor.cpp
 * @brief Scene 上のオブジェクト 1 個への窓。
 */
#include "Pch.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"


namespace fang
{
	Actor::Actor(Scene& scene, ActorHandle handle)
		: m_scene(&scene)
		, m_handle(handle)
	{
	}


	bool Actor::IsValid() const
	{
		return m_scene != nullptr && m_scene->IsValid(m_handle);
	}


	bool Actor::IsPendingDestroy() const
	{
		return m_scene != nullptr && m_scene->IsPendingDestroy(m_handle);
	}


	bool Actor::SetTransform(const Vector3& position, float rotationYRadians)
	{
		if (m_scene == nullptr)
		{
			return false;
		}

		return m_scene->SetLocalTransform(m_handle, position, rotationYRadians);
	}


	bool Actor::SetLocalMatrix(const Matrix4x4& localMatrix)
	{
		if (m_scene == nullptr)
		{
			return false;
		}

		return m_scene->SetLocalMatrix(m_handle, localMatrix);
	}


	Matrix4x4 Actor::GetWorldMatrix() const
	{
		if (m_scene == nullptr)
		{
			return Matrix4x4{};
		}

		return m_scene->GetWorldMatrix(m_handle);
	}


	Vector3 Actor::GetWorldPosition() const
	{
		const Matrix4x4 worldMatrix = GetWorldMatrix();
		return Vector3{ worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2] };
	}


	bool Actor::SetSkinningMatrices(std::span<const Matrix4x4> matrices)
	{
		if (m_scene == nullptr)
		{
			return false;
		}

		return m_scene->SetSkinningMatrices(m_handle, matrices);
	}


	MeshRendererComponent* Actor::GetMeshRendererComponent() const
	{
		if (m_scene == nullptr)
		{
			return nullptr;
		}

		return m_scene->GetMeshRendererComponent(m_handle);
	}


	ColliderComponent* Actor::GetColliderComponent() const
	{
		if (m_scene == nullptr)
		{
			return nullptr;
		}

		return m_scene->GetColliderComponent(m_handle);
	}


	HealthComponent* Actor::GetHealthComponent() const
	{
		if (m_scene == nullptr)
		{
			return nullptr;
		}

		return m_scene->GetHealthComponent(m_handle);
	}


	void Actor::Destroy()
	{
		if (m_scene == nullptr)
		{
			return;
		}

		m_scene->DestroyObject(m_handle);
	}


	Actor Actor::GetActorFromHandle(ActorHandle handle) const
	{
		if (m_scene == nullptr)
		{
			return Actor{};
		}

		return Actor{ *m_scene, handle };
	}


	Actor Actor::GetActorFromIndex(uint32_t index) const
	{
		if (m_scene == nullptr)
		{
			return Actor{};
		}

		return m_scene->GetActorFromIndex(index);
	}


	const Actor* FindFirstLiving(std::span<const Actor> actors)
	{
		for (const Actor& actor : actors)
		{
			if (actor.IsValid())
			{
				return &actor;
			}
		}

		return nullptr;
	}


	uint32_t CountLiving(std::span<const Actor> actors)
	{
		uint32_t livingCount = 0;
		for (const Actor& actor : actors)
		{
			if (actor.IsValid())
			{
				++livingCount;
			}
		}

		return livingCount;
	}
} // namespace fang
