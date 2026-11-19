/**
 * @file GameObjectManager.cpp
 * @brief ゲームオブジェクトを管理するクラスの実装
 */
#include "GameObjectManager.h"


namespace fangEngine::gameObject
{
	GameObjectManager::~GameObjectManager()
	{
		for (auto& group : m_objects)
		{
			for (auto& object : group)
			{
				object->OnDestroy();
			}
		}

		for (auto& object : m_pendingObjects)
		{
			object->OnDestroy();
		}
	}


	void GameObjectManager::StartPendingObjects()
	{
		// m_pendingObjectsをそのままループすると、
		// Start()の中でCreateObject()を呼んだ場合にm_pendingObjectsが書き換わってしまうため、
		// コピーしてからループする。
		std::vector<std::unique_ptr<IGameObject>> startingObjects;
		startingObjects.swap(m_pendingObjects);

		for (auto& object : startingObjects)
		{
			object->Start();
			m_objects[object->GetUpdateGroup()].push_back(std::move(object));
		}
	}


	void GameObjectManager::Update(EnUpdateGroup group)
	{
		for (auto& object : m_objects[group])
		{
			object->Update();
		}
	}
} // namespace fangEngine::gameObject
