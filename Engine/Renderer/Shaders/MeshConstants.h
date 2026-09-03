/**
 * @file MeshConstants.h
 * @brief メッシュ描画のルート定数。C++ と FXC の両方から include し、並びを 1 か所で決める。
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
	 *          デバイスロストするため。ライト（per-frame 相当）を per-draw に同居させているのは、
	 *          描く物が狼 1 体の今は分ける利得が無いため。cbPerFrame への分離は RenderGraph と一緒にやる。
	 */
	struct MeshObjectConstants
	{
		float4x4 modelViewProjection; /**< Multiply(world, viewProjection)。 */
		float4x4 world;               /**< ワールド法線・ワールド位置用。等倍前提。 */
		float4   directionToLight;    /**< xyz = 面から光源へ向かう向き（正規化済み）。w は未使用。 */
		float4   lightColor;          /**< rgb = リニア空間の色。w = 強さ。 */
		float4   ambientColor;        /**< rgb = 環境項。w は未使用。 */
		float4   cameraPosition;      /**< xyz = ワールドの視点。鏡面反射の視線ベクトル用。w は未使用。 */
		float4   material;            /**< x = metallic、y = roughness（知覚値）。z と w は未使用。 */
	};

#ifdef __cplusplus
	static_assert(sizeof(MeshObjectConstants) == 52 * 4, "HLSL 側が読む 52 DWORD と同じ大きさであること");
} // namespace fang
#endif

#endif // FANG_MESH_CONSTANTS_H
