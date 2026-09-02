// MeshVS.hlsl
// メッシュの頂点シェーダー。ルート定数で受け取った MVP でクリップ座標へ移す。
#include "Mesh.hlsli"

// C++ 側（Matrix4x4）は行優先ストレージ + 行ベクトル規約で、行列を転置せずそのまま渡してくる。
// HLSL の定数バッファは既定で列優先に読むので、ここで転置が掛かって辻褄が合う。
// ➡ mul(mvp, position) と「行列が左」で書くと、C++ の world * viewProjection と一致する。
// 片側だけ流儀を変えると、原因の分からない歪みになる。
cbuffer cbPerObject : register(b0)
{
	float4x4 mvp;
};

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(mvp, float4(input.position, 1.0));
	output.normal = input.normal;
	output.texCoord = input.texCoord;
	return output;
}
