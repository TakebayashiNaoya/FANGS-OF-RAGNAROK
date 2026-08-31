// ImGui.hlsli
// ImGui バックエンドの頂点シェーダーとピクセルシェーダーで共有する型。

struct VertexInput
{
	float2 position : POSITION;
	float2 texcoord : TEXCOORD;
	float4 color    : COLOR;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
	float2 texcoord : TEXCOORD;
};
