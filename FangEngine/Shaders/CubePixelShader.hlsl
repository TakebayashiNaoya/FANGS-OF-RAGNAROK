/**
 * @file CubePixelShader.hlsl
 * @brief 立方体のピクセルシェーダー
 */
struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

float4 main(VertexOutput input) : SV_TARGET
{
	return input.color;
}
