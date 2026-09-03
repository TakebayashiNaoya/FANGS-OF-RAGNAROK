/**
 * @file UnlitPS.hlsl
 * @brief 頂点色のピクセルシェーダー。ラスタライザが補間した頂点カラーをそのまま返す。
 */
#include "Unlit.hlsli"

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	return input.color;
}
