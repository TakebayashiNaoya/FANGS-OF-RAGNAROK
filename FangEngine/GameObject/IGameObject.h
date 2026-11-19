/**
 * @file IGameObject.h
 * @brief ゲームオブジェクトのインターフェース
 */
#pragma once
#include "Core/Noncopyable.h"
#include "UpdateGroup.h"


namespace fangEngine::gameObject
{
	// 前方宣言
	class GameObjectManager;


	/**
	 * @brief ゲームオブジェクトのインターフェース
	 */
	class IGameObject : private core::Noncopyable
	{
	public:
		virtual ~IGameObject() = default;

		/**
		 * @brief 更新グループを取得する
		 * @return 更新グループのID（デフォルトはenUpdateGroup_Gameplay）
		 */
		virtual EnUpdateGroup GetUpdateGroup() const { return EnUpdateGroup::enUpdateGroup_Gameplay; }

		/**
		 * @brief 破棄要求が出ているかどうかを取得する
		 * @return 破棄要求が出ている場合はtrue、そうでない場合はfalse
		 */
		bool IsDestroyRequested() const { return m_isDestroyRequested; }


	protected:
		/**
		 * @brief 初期化処理
		 * @details 生成直後に呼ばれるため、モデルの読み込みなど自分のことだけを行う処理を記述する
		 */
		virtual void Awake() {}

		/**
		 * @brief 初期化処理
		 * @details Startは次のフレームで呼ばれるため、他のオブジェクトの情報を参照する処理を記述する
		 */
		virtual void Start() {}

		/**
		 * @brief 更新処理
		 */
		virtual void Update() {}

		/**
		 * @brief 破棄処理
		 */
		virtual void OnDestroy() {}


	private:
		bool m_isDestroyRequested = false; // 破棄要求が出ているかどうか

		friend class GameObjectManager; // GameObjectManagerからのみアクセス可能
	};
} // namespace fangEngine::gameObject
