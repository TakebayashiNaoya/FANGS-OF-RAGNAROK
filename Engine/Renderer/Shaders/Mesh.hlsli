// Mesh.hlsli
// メッシュの頂点シェーダーとピクセルシェーダーで共有する型。
// 並びは MeshRenderer が private に持つ頂点構造体との契約。片方だけ変えない。

struct VertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 position : SV_POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
};
