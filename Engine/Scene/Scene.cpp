/**
 * @file Scene.cpp
 * @brief Scene の入れ物（席・世代付きハンドル・生成と破棄）。
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

		const bool hasAllBuffers = m_generations != nullptr && m_isActive != nullptr && m_pendingDestroy != nullptr &&
								   m_freeIndices != nullptr && m_pendingDestroyIndices != nullptr;
		if (!hasAllBuffers)
		{
			FANG_LOG_ERROR(Scene, "Scene の入れ物を確保できなかった");

			DeleteArray(allocator, m_pendingDestroyIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_freeIndices, desc.maxObjectCount);
			DeleteArray(allocator, m_pendingDestroy, desc.maxObjectCount);
			DeleteArray(allocator, m_isActive, desc.maxObjectCount);
			DeleteArray(allocator, m_generations, desc.maxObjectCount);

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
		for (uint32_t index = 0; index < m_maxObjectCount; ++index)
		{
			m_freeIndices[index] = m_maxObjectCount - 1 - index;
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

		DeleteArray(*m_allocator, m_pendingDestroyIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_freeIndices, m_maxObjectCount);
		DeleteArray(*m_allocator, m_pendingDestroy, m_maxObjectCount);
		DeleteArray(*m_allocator, m_isActive, m_maxObjectCount);
		DeleteArray(*m_allocator, m_generations, m_maxObjectCount);

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

		m_pendingDestroy[handle.index]                 = true;
		m_pendingDestroyIndices[m_pendingDestroyCount] = handle.index;
		++m_pendingDestroyCount;
	}


	bool Scene::IsValid(GameObjectHandle handle) const
	{
		return handle.IsValid() && handle.index < m_maxObjectCount && m_isActive[handle.index] &&
			   m_generations[handle.index] == handle.generation;
	}


	void Scene::Update(float deltaTimeSeconds)
	{
		FANG_UNUSED(deltaTimeSeconds); // 階層とコンポーネントの更新は Scene 4 / Scene 5 で積む。

		// 3. 破棄の予約を反映する（席を詰め、世代を進める）。
		for (uint32_t pendingIndex = 0; pendingIndex < m_pendingDestroyCount; ++pendingIndex)
		{
			const uint32_t index = m_pendingDestroyIndices[pendingIndex];

			m_isActive[index]       = false;
			m_pendingDestroy[index] = false;
			++m_generations[index];

			m_freeIndices[m_freeIndexCount] = index;
			++m_freeIndexCount;
		}

		m_pendingDestroyCount = 0;
	}
} // namespace fang
