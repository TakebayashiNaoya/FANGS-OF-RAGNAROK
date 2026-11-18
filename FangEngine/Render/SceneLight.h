/**
 * @file SceneLight.h
 * @brief シーンの光をまとめた構造体
 */
#pragma once
#include "Core/Math/Vector.h"


namespace fangEngine::render
{
	/**
	 * @brief ディレクションライトの情報をまとめた構造体
	 */
	struct DirectionLight
	{
		core::Vector3 direction; // 光が進む向き（単位ベクトル）
		core::Vector3 color;     // 光の色
	};


	/**
	 * @brief シーンの光をまとめた構造体
	 */
	struct SceneLight
	{
		DirectionLight directionLight; // ディレクションライト
		core::Vector3  ambientColor;   // アンビエントライトの色
		// TODO：ポイントライトとスポットライトは後で追加する
	};
} // namespace fangEngine::render
