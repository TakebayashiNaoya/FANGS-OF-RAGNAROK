/**
 * @file Scene.cpp
 * @brief Scene の入れ物（席・世代付きハンドル・生成と破棄・Transform 階層）。
 */
#include "Pch.h"
#include "Scene/Scene.h"
#include "Core/Log/Assert.h"
#include "Core/Memory/Allocator.h"
#include "Scene/SceneLog.h"


FANG_DEFINE_LOG_CATEGORY(Scene);


namespace fang
{
	const char* GetSceneModuleName()
	{
		return "Scene";
	}


	Scene::~Scene()
	{
		Shutdown();
	}


	bool Scene::Initialize(IAllocator& allocator, const SceneDesc& desc)
	{
		FANG_ASSERT(m_allocator == nullptr, "二重に初期化しようとしている");

		if (desc.maxObjectCount == 0)
		{
			FANG_LOG_ERROR(Scene, "Scene の maxObjectCount が 0");
			return false;
		}

		m_generations           = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_isActive              = NewArray<bool>(allocator, desc.maxObjectCount);
		m_pendingDestroy        = NewArray<bool>(allocator, desc.maxObjectCount);
		m_freeIndices           = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_pendingDestroyIndices = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_localMatrices         = NewArray<Matrix4x4>(allocator, desc.maxObjectCount);
		m_worldMatrices         = NewArray<Matrix4x4>(allocator, desc.maxObjectCount);
		m_parentIndices         = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_firstChildIndices     = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_nextSiblingIndices    = NewArray<uint32_t>(allocator, desc.maxObjectCount);
		m_indexStack            = NewArray<uint32_t>(allocator, desc.maxObjectCount);

		const bool hasAllBuffers = m_generations != nullptr && m_isActive != nullptr && m_pendingDestroy != nullptr &&
								   m_freeIndices != nullptr && m_pendingDestroyIndices != nullptr &&
								   m_localMatrices != nullptr && m_worldMatrices != nullptr &&
								   m_parentIndices != nullptr && m_firstChildIndices != nullptr &&
								   m_nextSiblingIndices != nullptr && m_indexStack != nullptr;
		if (!hasAllBuffers)
		{
			FANG_LOG_ERROR(Scene, "Scene の入れ物を確保できなかった");

			DeleteArray(allocator, m_indexStack, desc.maxObjectCount);
			DeleteArray(allocator, m_nextSiblingIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_firstChildIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_parentIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_worldMatrices, desc.maxObjectCount);
			DeleteArray(allocator, m_localMatrices, desc.maxObjectCount);
			DeleteArray(allocator, m_pendingDestroyIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_freeIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_pendingDestroy, desc.maxObjectCount);
			DeleteArray(allocator, m_isActive, desc.maxObjectCount);
			DeleteArray(allocator, m_generations, desc.maxObjectCount);

			m_indexStack            = nullptr;
			m_nextSiblingIndices    = nullptr;
			m_firstChildIndices     = nullptr;
			m_parentIndices         = nullptr;
			m_worldMatrices         = nullptr;
			m_localMatrices         = nullptr;
			m_pendingDestroyIndices = nullptr;
			m_freeIndices           = nullptr;
			m_pendingDestroy        = nullptr;
			m_isActive              = nullptr;
			m_generations           = nullptr;
			return false;
		}

		m_allocator      = &allocator;
		m_maxObjectCount = desc.maxObjectCount;

		// 空き番号は末尾から積む ➡ 先頭（0 番）から順に配られる。
		// 親子リストは「無し」を表す番号（INVALID_INDEX）で埋めておく。0 は有効なスロット番号なので、
		// NewArray の既定値（0 埋め）のままにはできない。
		for (uint32_t index = 0; index < m_maxObjectCount; ++index)
		{
			m_freeIndices[index]        = m_maxObjectCount - 1 - index;
			m_parentIndices[index]      = GameObjectHandle::INVALID_INDEX;
			m_firstChildIndices[index]  = GameObjectHandle::INVALID_INDEX;
			m_nextSiblingIndices[index] = GameObjectHandle::INVALID_INDEX;
		}
		m_freeIndexCount = m_maxObjectCount;

		FANG_LOG_INFO(Scene, "Scene を作った: オブジェクト上限 {}", m_maxObjectCount);

		return true;
	}


	void Scene::Shutdown()
	{
		if (m_allocator == nullptr)
		{
			return;
		}

		DeleteArray(*m_allocator, m_indexStack, m_maxObjectCount);
		DeleteArray(*m_allocator, m_nextSiblingIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_firstChildIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_parentIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_worldMatrices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_localMatrices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_pendingDestroyIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_freeIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_pendingDestroy, m_maxObjectCount);
		DeleteArray(*m_allocator, m_isActive, m_maxObjectCount);
		DeleteArray(*m_allocator, m_generations, m_maxObjectCount);

		m_indexStack            = nullptr;
		m_nextSiblingIndices    = nullptr;
		m_firstChildIndices     = nullptr;
		m_parentIndices         = nullptr;
		m_worldMatrices         = nullptr;
		m_localMatrices         = nullptr;
		m_pendingDestroyIndices = nullptr;
		m_freeIndices           = nullptr;
		m_pendingDestroy        = nullptr;
		m_isActive              = nullptr;
		m_generations           = nullptr;

		m_maxObjectCount      = 0;
		m_freeIndexCount      = 0;
		m_pendingDestroyCount = 0;
		m_allocator           = nullptr;
	}


	GameObjectHandle Scene::CreateObject()
	{
		if (m_freeIndexCount == 0)
		{
			FANG_LOG_WARNING(Scene, "オブジェクトの上限（{}）に達したので作れなかった", m_maxObjectCount);
			return GameObjectHandle{};
		}

		const uint32_t index = m_freeIndices[--m_freeIndexCount];

		m_isActive[index]       = true;
		m_pendingDestroy[index] = false;

		return GameObjectHandle{ index, m_generations[index] };
	}


	void Scene::DestroyObject(GameObjectHandle handle)
	{
		if (!IsValid(handle) || m_pendingDestroy[handle.index])
		{
			return;
		}

		// 子を含めて丸ごと破棄予約する（明示スタック。再帰もヒープ確保もしない）。
		uint32_t stackTop      = 0;
		m_indexStack[stackTop] = handle.index;
		++stackTop;

		while (stackTop > 0)
		{
			--stackTop;
			const uint32_t index = m_indexStack[stackTop];

			m_pendingDestroy[index]                        = true;
			m_pendingDestroyIndices[m_pendingDestroyCount] = index;
			++m_pendingDestroyCount;

			for (uint32_t child = m_firstChildIndices[index]; child != GameObjectHandle::INVALID_INDEX;
				 child          = m_nextSiblingIndices[child])
			{
				m_indexStack[stackTop] = child;
				++stackTop;
			}
		}
	}


	bool Scene::IsValid(GameObjectHandle handle) const
	{
		return handle.IsValid() && handle.index < m_maxObjectCount && m_isActive[handle.index] &&
			   m_generations[handle.index] == handle.generation;
	}


	void Scene::Update(float deltaTimeSeconds)
	{
		FANG_UNUSED(deltaTimeSeconds); // コンポーネントの更新は Scene 5 で積む。

		// 3. 破棄の予約を反映する（子リストから外し、席を詰め、世代を進める）。
		for (uint32_t pendingIndex = 0; pendingIndex < m_pendingDestroyCount; ++pendingIndex)
		{
			const uint32_t index = m_pendingDestroyIndices[pendingIndex];

			RemoveFromParentChildList(index);

			m_isActive[index]       = false;
			m_pendingDestroy[index] = false;
			++m_generations[index];

			m_parentIndices[index]      = GameObjectHandle::INVALID_INDEX;
			m_firstChildIndices[index]  = GameObjectHandle::INVALID_INDEX;
			m_nextSiblingIndices[index] = GameObjectHandle::INVALID_INDEX;
			m_localMatrices[index]      = Matrix4x4{};

			m_freeIndices[m_freeIndexCount] = index;
			++m_freeIndexCount;
		}

		m_pendingDestroyCount = 0;

		// 4. 根から順にワールド行列を作る（明示スタック。再帰もヒープ確保もしない）。
		uint32_t stackTop = 0;
		for (uint32_t index = 0; index < m_maxObjectCount; ++index)
		{
			if (m_isActive[index] && m_parentIndices[index] == GameObjectHandle::INVALID_INDEX)
			{
				m_indexStack[stackTop] = index;
				++stackTop;
			}
		}

		while (stackTop > 0)
		{
			--stackTop;
			const uint32_t index       = m_indexStack[stackTop];
			const uint32_t parentIndex = m_parentIndices[index];

			m_worldMatrices[index] = (parentIndex == GameObjectHandle::INVALID_INDEX)
										 ? m_localMatrices[index]
										 : Multiply(m_localMatrices[index], m_worldMatrices[parentIndex]);

			for (uint32_t child = m_firstChildIndices[index]; child != GameObjectHandle::INVALID_INDEX;
				 child          = m_nextSiblingIndices[child])
			{
				m_indexStack[stackTop] = child;
				++stackTop;
			}
		}
	}


	bool Scene::SetLocalMatrix(GameObjectHandle handle, const Matrix4x4& localMatrix)
	{
		if (!IsValid(handle))
		{
			return false;
		}

		m_localMatrices[handle.index] = localMatrix;
		return true;
	}


	bool Scene::SetLocalTransform(GameObjectHandle handle, const Vector3& position, float rotationYRadians)
	{
		Matrix4x4 localMatrix = MakeRotationYMatrix(rotationYRadians);
		localMatrix.m[3][0]   = position.x;
		localMatrix.m[3][1]   = position.y;
		localMatrix.m[3][2]   = position.z;

		return SetLocalMatrix(handle, localMatrix);
	}


	Matrix4x4 Scene::GetLocalMatrix(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return Matrix4x4{};
		}

		return m_localMatrices[handle.index];
	}


	Matrix4x4 Scene::GetWorldMatrix(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return Matrix4x4{};
		}

		return m_worldMatrices[handle.index];
	}


	bool Scene::SetParent(GameObjectHandle handle, GameObjectHandle parent)
	{
		if (!IsValid(handle))
		{
			return false;
		}

		const bool hasNewParent = parent.IsValid();
		if (hasNewParent)
		{
			if (!IsValid(parent) || parent.index == handle.index)
			{
				return false;
			}

			// 先祖をたどって自分に出会ったら輪ができる。
			for (uint32_t ancestor = m_parentIndices[parent.index]; ancestor != GameObjectHandle::INVALID_INDEX;
				 ancestor          = m_parentIndices[ancestor])
			{
				if (ancestor == handle.index)
				{
					return false;
				}
			}
		}

		RemoveFromParentChildList(handle.index);

		if (hasNewParent)
		{
			m_parentIndices[handle.index]      = parent.index;
			m_nextSiblingIndices[handle.index] = m_firstChildIndices[parent.index];
			m_firstChildIndices[parent.index]  = handle.index;
		}
		else
		{
			m_parentIndices[handle.index]      = GameObjectHandle::INVALID_INDEX;
			m_nextSiblingIndices[handle.index] = GameObjectHandle::INVALID_INDEX;
		}

		return true;
	}


	GameObjectHandle Scene::GetParent(GameObjectHandle handle) const
	{
		if (!IsValid(handle))
		{
			return GameObjectHandle{};
		}

		const uint32_t parentIndex = m_parentIndices[handle.index];
		if (parentIndex == GameObjectHandle::INVALID_INDEX)
		{
			return GameObjectHandle{};
		}

		return GameObjectHandle{ parentIndex, m_generations[parentIndex] };
	}


	void Scene::RemoveFromParentChildList(uint32_t index)
	{
		const uint32_t parentIndex = m_parentIndices[index];
		if (parentIndex == GameObjectHandle::INVALID_INDEX)
		{
			return;
		}

		uint32_t* link = &m_firstChildIndices[parentIndex];
		while (*link != GameObjectHandle::INVALID_INDEX)
		{
			if (*link == index)
			{
				*link = m_nextSiblingIndices[index];
				return;
			}

			link = &m_nextSiblingIndices[*link];
		}
	}
} // namespace fang
