// TriangleVS.hlsl
// 三角形の頂点シェーダー。頂点は NDC 直書きなので、そのまま通す。
#include "Triangle.hlsli"

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = float4(input.position, 1.0);
	output.color = input.color;
	return output;
}
