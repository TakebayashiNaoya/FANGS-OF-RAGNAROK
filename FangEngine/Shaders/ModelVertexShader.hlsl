/**
 * @file ModelVertexShader.hlsl
 * @brief モデルの頂点シェーダー
 */
struct VertexInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float3 normal : NORMAL;
};

cbuffer ObjectConstants : register(b0)
{
	row_major float4x4 worldViewProjection;
};

VertexOutput main(VertexInput input)
{
	VertexOutput output;
	// 左が行ベクトル、右が列ベクトル。DirectXMathと同じ行ベクトルの決まりで行列を作ったから、頂点は左に置く。
	output.position = mul(float4(input.position, 1.0f), worldViewProjection);
	output.normal = input.normal;
	return output;
}
