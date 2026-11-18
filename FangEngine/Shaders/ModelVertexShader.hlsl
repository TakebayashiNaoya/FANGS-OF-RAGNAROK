/**
 * @file ModelVertexShader.hlsl
 * @brief モデルの頂点シェーダー
 */
struct VertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD0;
	float2 secondUv : TEXCOORD1;
	float4 tangent  : TANGENT;
};

struct VertexOutput
{
	float4 position     : SV_POSITION;
	float3 worldNormal  : NORMAL;
	float2 uv           : TEXCOORD0;
	float2 secondUv     : TEXCOORD1;
	float4 worldTangent : TANGENT;
};

cbuffer ObjectConstants : register(b0)
{
	row_major float4x4 worldViewProjection;
	row_major float4x4 world;
};

VertexOutput main(VertexInput input)
{
	VertexOutput output;
	// 左が行ベクトル、右が列ベクトル。DirectXMathと同じ行ベクトルの決まりで行列を作ったから、頂点は左に置く。
	output.position     = mul(float4(input.position, 1.0f), worldViewProjection);
	output.worldNormal  = mul(input.normal, (float3x3)world);
	output.uv           = input.uv;
	output.secondUv     = input.secondUv;
	output.worldTangent = float4(mul(input.tangent.xyz, (float3x3)world), input.tangent.w);
	return output;
}
