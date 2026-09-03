/**
 * @file TerrainConstants.h
 * @brief 地形描画の定数バッファ。C++ と FXC の両方から include し、並びを 1 か所で決める。
 * @details このファイルは UTF-8 の BOM を付けない。FXC が BOM を読めない（X3000）ため。
 *          MSVC 側は /utf-8（Common.props）が付いているので BOM が無くても正しく読む。
 */
#ifndef FANG_TERRAIN_CONSTANTS_H
#define FANG_TERRAIN_CONSTANTS_H

#ifdef __cplusplus
#include "Core/Math/Vector4.h"

namespace fang
{
	// HLSL の綴りに合わせる。並びがずれていないかは下の static_assert が見張る。
	using float4 = Vector4;
#endif

	/**
	 * @brief 地形を描くときに b0 のルート CBV で渡すもの。
	 * @details 地形は動かないので、読み込みのときに 1 回書くだけで毎フレームの更新が無い。
	 *          メンバを float4 だけにして、HLSL の 16 バイトパッキングと C++ の並びがずれる余地を消してある。
	 */
	struct TerrainConstants
	{
		/**
		 * @brief x = 地形の X 半幅（cm）、y = Z 半幅（cm）、z = レイヤ UV のスケール（1/cm）、
		 *        w = スプラットマップの半テクセル（UV 幅）。
		 */
		float4 sizeParameters;

		float4 layerRoughness; /**< xyz = 3 レイヤの知覚 roughness。w は未使用。 */
	};

#ifdef __cplusplus
	static_assert(sizeof(TerrainConstants) == 8 * 4, "HLSL 側が読む 8 DWORD と同じ大きさであること");
} // namespace fang
#endif

#endif // FANG_TERRAIN_CONSTANTS_H
