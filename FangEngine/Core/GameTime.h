/**
 * @file GameTime.h
 * @brief ゲームの時間を管理するクラス
 */
#pragma once
#include "Core/Noncopyable.h"


namespace fangEngine::core
{
	/**
	 * @brief ゲームの時間を管理するクラス
	 */
	class GameTime : private core::Noncopyable
	{
	public:
		/**
		 * @brief シングルトンインスタンスを取得する
		 * @return シングルトンインスタンス
		 */
		static GameTime& GetInstance()
		{
			static GameTime instance;
			return instance;
		}

		/**
		 * @brief 前フレームからの経過時間を設定する
		 * @param deltaTime 前フレームからの経過時間（秒）
		 */
		void SetDeltaTime(float deltaTime) { m_deltaTime = deltaTime; }

		/**
		 * @brief 前フレームからの経過時間を取得する
		 * @return 前フレームからの経過時間（秒）
		 */
		float GetDeltaTime() const { return m_deltaTime; }


	private:
		GameTime() = default; // シングルトン

		float m_deltaTime = 0.0f; // 前フレームからの経過時間
	};
} // namespace fangEngine::core
