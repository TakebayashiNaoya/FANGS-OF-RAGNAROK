/**
 * @file ModelPixelShader.hlsl
 * @brief モデルのピクセルシェーダー
 */
struct VertexOutput
{
	float4 position : SV_POSITION;
	float3 normal : NORMAL;
};

float4 main(VertexOutput input) : SV_TARGET
{
	return float4(input.normal * 0.5f + 0.5f, 1.0f);
}
