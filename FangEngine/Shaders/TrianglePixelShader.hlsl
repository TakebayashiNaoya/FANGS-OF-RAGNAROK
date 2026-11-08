/**
 * @file TrianglePixelShader.hlsl
 * @brief 三角形のピクセルシェーダー
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
