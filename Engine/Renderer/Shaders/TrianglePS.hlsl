/**
 * @file TrianglePS.hlsl
 * @brief 三角形のピクセルシェーダー。ラスタライザが補間した頂点カラーをそのまま返す。
 */
#include "Triangle.hlsli"

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	return input.color;
}
