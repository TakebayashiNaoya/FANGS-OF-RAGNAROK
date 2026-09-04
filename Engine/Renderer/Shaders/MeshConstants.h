/**
 * @file MeshConstants.h
 * @brief メッシュ描画の定数バッファ。C++ と FXC の両方から include し、並びを 1 か所で決める。
 * @details このファイルだけは UTF-8 の BOM を付けない。FXC が BOM を読めない（X3000）ため。
 *          MSVC 側は /utf-8（Common.props）が付いているので BOM が無くても正しく読む。
 */
#ifndef FANG_MESH_CONSTANTS_H
#define FANG_MESH_CONSTANTS_H

#ifdef __cplusplus
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector4.h"

namespace fang
{
	// HLSL の綴りに合わせる。並びがずれていないかは下の static_assert が見張る。
	using float4   = Vector4;
	using float4x4 = Matrix4x4;
#endif

	/**
	 * @brief メッシュ 1 個を描くときに b0 のルート CBV で渡すもの。
	 * @details メンバを float4 / float4x4 だけにして、HLSL の 16 バイトパッキングと C++ の並びが
	 *          ずれる余地を消してある。行列は行優先のまま転置せずに渡す（HLSL 側が列優先に読んで辻褄が合う）。
	 *          ルート定数にしないのは、実機のドライバが 16 DWORD 超のルート定数のパイプライン生成で
	 *          デバイスロストするため。
	 */
	struct MeshObjectConstants
	{
		float4x4 world;    /**< ワールド法線・ワールド位置用。等倍前提。 */
		float4   material; /**< x = metallic、y = roughness（知覚値）。z と w は未使用。 */
	};

	/**
	 * @brief 1 フレームの間ずっと同じ値を置く b1 のルート CBV。
	 * @details 視点と光は描画物が変わっても変わらないので b0 から分けてある ➡ 描画物ごとに積み直すのは
	 *          world と材質だけで済む。並びの決め方は MeshObjectConstants と同じ。
	 */
	struct MeshFrameConstants
	{
		float4x4 viewProjection;   /**< Multiply(ビュー行列, 透視投影行列)。行ベクトル規約なのでビューが左。 */
		float4   cameraPosition;   /**< xyz = ワールドの視点。鏡面反射の視線ベクトル用。w は未使用。 */
		float4   directionToLight; /**< xyz = 面から光源へ向かう向き（正規化済み）。w は未使用。 */
		float4   lightColor;       /**< rgb = リニア空間の色。w = 強さ。 */
		float4   ambientColor;     /**< rgb = 環境項。w は未使用。 */

		float4x4 lightViewProjection; /**< 光の View ➡ 正射影。影の判定でワールド位置を光のクリップ空間へ移す。 */
		float4 shadowParameters; /**< x = シャドウマップ 1 テクセルの UV 幅。y = 影の有効(0 か 1)。z と w は未使用。 */
	};

#ifdef __cplusplus
	static_assert(sizeof(MeshObjectConstants) == 20 * 4, "HLSL 側が読む 20 DWORD と同じ大きさであること");
	static_assert(sizeof(MeshFrameConstants) == 52 * 4, "HLSL 側が読む 52 DWORD と同じ大きさであること");
} // namespace fang
#endif

#endif // FANG_MESH_CONSTANTS_H
