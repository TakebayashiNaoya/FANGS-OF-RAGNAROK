/**
 * @file GameObjectManager.h
 * @brief ゲームオブジェクトを管理するクラス
 */
#pragma once
#include <array>
#include <memory>
#include <vector>
#include <type_traits>
#include <utility>
#include "Core/Noncopyable.h"
#include "IGameObject.h"


namespace fangEngine::gameObject
{
	/**
	 * @brief ゲームオブジェクトを管理するクラス
	 */
	class GameObjectManager : private core::Noncopyable
	{
	public:
		~GameObjectManager();

		/**
		 * @brief ゲームオブジェクトを作成する
		 * @tparam T 作成するゲームオブジェクトの型
		 * @tparam Args コンストラクタの引数の型
		 * @param args コンストラクタの引数
		 * @return 作成されたゲームオブジェクトのポインタ
		 */
		template <typename T, typename... Args> T* CreateObject(Args&&... args);

		/**
		 * @brief 待ち列にあるゲームオブジェクトをStart()を呼んでグループの列に移す
		 */
		void StartPendingObjects();

		/**
		 * @brief ゲームオブジェクトを更新する
		 * @param group 更新グループ
		 */
		void Update(EnUpdateGroup group);


	private:
		std::vector<std::unique_ptr<IGameObject>> m_pendingObjects; // 待機中のゲームオブジェクト

		std::array<std::vector<std::unique_ptr<IGameObject>>, enUpdateGroup_Num> m_objects; // 更新グループごとのゲームオブジェクト
	};


	template <typename T, typename... Args> T* GameObjectManager::CreateObject(Args&&... args)
	{
		static_assert(std::is_base_of_v<IGameObject, T>, "IGameObjectの派生クラスを指定してください");
		std::unique_ptr<IGameObject> object = std::make_unique<T>(std::forward<Args>(args)...);
		T* ptr = static_cast<T*>(object.get());
		object->Awake();
		m_pendingObjects.push_back(std::move(object));
		return ptr;
	}
} // namespace fangEngine::gameObject
