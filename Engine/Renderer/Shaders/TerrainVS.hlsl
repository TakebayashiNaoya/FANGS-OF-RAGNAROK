/**
 * @file TerrainVS.hlsl
 * @brief 地形の頂点シェーダー。ワールド座標の頂点をそのままビュー射影で移す。
 * @details 地形はワールド行列を持たない（頂点がワールド座標で生成済み）ので、b0 は使わない。
 *          b1 はシーン View と同じ MeshFrameConstants を借りる ➡ 視点が建物と常に一致する。
 */
#include "Terrain.hlsli"
#include "MeshConstants.h"

/** @brief フレームの間ずっと同じ定数。並びは MeshConstants.h の MeshFrameConstants。 */
cbuffer cbFrame : register(b1)
{
	MeshFrameConstants frameConstants;
};

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(frameConstants.viewProjection, float4(input.position, 1.0));
	output.worldPosition = input.position;
	output.normal = input.normal;
	return output;
}
