/**
 * @file Triangle.hlsli
 * @brief 三角形の頂点シェーダーとピクセルシェーダーで共有する型。
 */

/** @brief 頂点シェーダーへの入力。 */
struct VertexInput
{
	float3 position : POSITION;
	float4 color    : COLOR;
};

/** @brief 頂点シェーダーからピクセルシェーダーへ渡すもの。 */
struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
};
