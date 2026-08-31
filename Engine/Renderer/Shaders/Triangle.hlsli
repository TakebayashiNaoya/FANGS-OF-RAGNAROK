// Triangle.hlsli
// 三角形の頂点シェーダーとピクセルシェーダーで共有する型。

struct VertexInput
{
	float3 position : POSITION;
	float4 color    : COLOR;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
};
