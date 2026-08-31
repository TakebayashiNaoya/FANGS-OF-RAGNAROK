// ImGuiVS.hlsl
// ImGui の頂点をスクリーン座標から NDC へ変換する。
#include "ImGui.hlsli"

cbuffer VertexConstants : register(b0)
{
	float4x4 projectionMatrix;
};

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(projectionMatrix, float4(input.position, 0.0, 1.0));
	output.color = input.color;
	output.texcoord = input.texcoord;
	return output;
}
