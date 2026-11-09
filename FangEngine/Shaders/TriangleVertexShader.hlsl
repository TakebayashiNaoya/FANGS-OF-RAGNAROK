/**
 * @file TriangleVertexShader.hlsl
 * @brief 三角形の頂点シェーダー
 */
struct VertexInput
{
	float3 position : POSITION;
	float4 color : COLOR;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

cbuffer ObjectConstants : register(b0)
{
	row_major float4x4 world;
};

VertexOutput main(VertexInput input)
{
	VertexOutput output;
	// 左が行ベクトル、右が列ベクトル。DirectXMathと同じ行ベクトルの決まりで行列を作ったから、頂点は左に置く。
	output.position = mul(float4(input.position, 1.0f), world);
	output.color = input.color;
	return output;
}
