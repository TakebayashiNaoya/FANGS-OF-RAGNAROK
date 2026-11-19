/**
 * @file UpdateGroup.h
 * @brief アップデートを回す順番のIDを定義するヘッダーファイル
 */
#pragma once


namespace fangEngine::gameObject
{
	/**
	 * @brief アップデートを回す順番のID
	 * @details この並びのままにアップデートが回る
	 */
	enum EnUpdateGroup
	{
		enUpdateGroup_PreUpdate,        // Gameplay以降で必要になる値を予め用意するためのグループ
		enUpdateGroup_Gameplay,         // 自分以外を制御するためのグループ（プレイヤーやボスなど）
		enUpdateGroup_ParallelGameplay, // 自分自身しか制御せず、数が多い処理を並列で回すためのグループ（雑魚敵など）
		enUpdateGroup_PostPhysics,      // 着地や押し戻しの結果を読むグループ
		enUpdateGroup_PostAnimation,    // アニメーション後に必要になる値を更新するためのグループ
		enUpdateGroup_Camera,           // カメラの更新に使用するグループ
		enUpdateGroup_Late,             // 他のすべての更新が完了した後に実行されるグループ
		enUpdateGroup_Num
	};
} // namespace fangEngine::gameObject
