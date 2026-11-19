/**
 * @file GameObjectManager.cpp
 * @brief ゲームオブジェクトを管理するクラスの実装
 */
#include "GameObjectManager.h"


namespace fangEngine::gameObject
{
	GameObjectManager::~GameObjectManager()
	{
		for (auto& group : m_objects) {
			for (auto& object : group) {
				object->OnDestroy();
			}
		}

		for (auto& object : m_pendingObjects) {
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

		for (auto& object : startingObjects) {
			if (object->IsDestroyRequested() == false) {
				object->Start();
			}
			m_objects[object->GetUpdateGroup()].push_back(std::move(object));
		}
	}


	void GameObjectManager::Update(EnUpdateGroup group)
	{
		for (auto& object : m_objects[group]) {
			if (object->IsDestroyRequested() == false) {
				object->Update();
			}
		}
	}


	void GameObjectManager::DestroyRequestedObjects()
	{
		// OnDestroy()の中でCreateObjectが呼ばれることがあるため、m_objectsの中身を直接ループせずに、
		// 破棄するものを別の配列に移してからOnDestroy()を呼ぶ。
		std::vector<std::unique_ptr<IGameObject>> destroyObjects;

		// m_pendingObjectsの中で破棄要求が出ているものを探す
		for (auto& object : m_pendingObjects) {
			if (object->IsDestroyRequested()) {
				destroyObjects.push_back(std::move(object));
			}
		}
		std::erase(m_pendingObjects, nullptr);

		// m_objectsの中で破棄要求が出ているものを探す
		for (auto& objects : m_objects) {
			for (auto& object : objects) {
				if (object->IsDestroyRequested()) {
					destroyObjects.push_back(std::move(object));
				}
			}
			std::erase(objects, nullptr);
		}

		// 破棄要求が出ているものをOnDestroy()する
		for (auto& object : destroyObjects) {
			object->OnDestroy();
		}
	}
} // namespace fangEngine::gameObject
