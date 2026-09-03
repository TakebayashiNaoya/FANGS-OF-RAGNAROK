/**
 * @file UnlitConstants.h
 * @brief 頂点色描画の定数バッファ。C++ と FXC の両方から include し、並びを 1 か所で決める。
 * @details このファイルは MeshConstants.h と同じく UTF-8 の BOM を付けない。FXC が BOM を読めない（X3000）ため。
 *          MSVC 側は /utf-8（Common.props）が付いているので BOM が無くても正しく読む。
 */
#ifndef FANG_UNLIT_CONSTANTS_H
#define FANG_UNLIT_CONSTANTS_H

#ifdef __cplusplus
#include "Core/Math/Matrix4x4.h"

namespace fang
{
	// HLSL の綴りに合わせる。並びがずれていないかは下の static_assert が見張る。
	using float4x4 = Matrix4x4;
#endif

	/**
	 * @brief 頂点色のものを 1 個描くときに b0 のルート CBV で渡すもの。
	 * @details 行列は行優先のまま転置せずに渡す（HLSL 側が列優先に読んで辻褄が合う）。
	 *          16 DWORD ならルート定数にも載るが、b0 の口は MeshRenderer と揃えてルート CBV にしてある
	 *          ➡ 実機のドライバがルート定数のパイプライン生成で倒れた経緯があり、実績のある形を選んだ。
	 */
	struct UnlitObjectConstants
	{
		float4x4 transform; /**< 頂点をクリップ空間へ移す合成済み行列。 */
	};

#ifdef __cplusplus
	static_assert(sizeof(UnlitObjectConstants) == 16 * 4, "HLSL 側が読む 16 DWORD と同じ大きさであること");
} // namespace fang
#endif

#endif // FANG_UNLIT_CONSTANTS_H
