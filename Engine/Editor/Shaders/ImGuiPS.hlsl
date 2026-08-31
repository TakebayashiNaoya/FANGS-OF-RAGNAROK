// ImGuiPS.hlsl
// フォントアトラスをサンプリングして頂点カラーと掛ける。
#include "ImGui.hlsli"

Texture2D atlasTexture : register(t0);
SamplerState atlasSampler : register(s0);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	return input.color * atlasTexture.Sample(atlasSampler, input.texcoord);
}
