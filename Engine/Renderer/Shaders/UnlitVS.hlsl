/**
 * @file UnlitVS.hlsl
 * @brief 頂点色の頂点シェーダー。b0 の合成済み行列でクリップ座標へ移す。
 */
#include "Unlit.hlsli"
#include "UnlitConstants.h"

/**
 * @brief 描くもの 1 個ぶんの定数。
 * @details C++ 側（Matrix4x4）は行優先ストレージ + 行ベクトル規約で、行列を転置せずそのまま渡してくる。
 *          HLSL の定数バッファは既定で列優先に読むので、ここで転置が掛かって辻褄が合う。
 *          ➡ mul(行列, ベクトル) と「行列が左」で書く。恒等行列を渡せば座標がそのまま通る。
 */
cbuffer cbObject : register(b0)
{
	UnlitObjectConstants objectConstants;
};

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(objectConstants.transform, float4(input.position, 1.0));
	output.color = input.color;
	return output;
}
