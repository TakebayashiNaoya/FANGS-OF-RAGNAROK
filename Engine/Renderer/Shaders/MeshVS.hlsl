/**
 * @file MeshVS.hlsl
 * @brief メッシュの頂点シェーダー。MVP でクリップ座標へ移し、ライティング用にワールド法線とワールド位置を作る。
 */
#include "Mesh.hlsli"
#include "MeshConstants.h"

/**
 * @brief 描くもの 1 個ぶんの定数。
 * @details C++ 側（Matrix4x4）は行優先ストレージ + 行ベクトル規約で、行列を転置せずそのまま渡してくる。
 *          HLSL の定数バッファは既定で列優先に読むので、ここで転置が掛かって辻褄が合う。
 *          ➡ mul(constants.mvp, position) と「行列が左」で書くと、C++ の world * viewProjection と一致する。
 *          片側だけ流儀を変えると、原因の分からない歪みになる。
 */
cbuffer cbPerObject : register(b0)
{
	MeshObjectConstants constants;
};

VertexOutput VertexMain(VertexInput input)
{
	VertexOutput output;
	output.position = mul(constants.modelViewProjection, float4(input.position, 1.0));
	output.worldPosition = mul(constants.world, float4(input.position, 1.0)).xyz;

	// w = 0 で掛けると平行移動が効かない ➡ 法線は向きだけが回る。等倍前提なので逆転置は要らない。
	output.normal = mul(constants.world, float4(input.normal, 0.0)).xyz;

	output.texCoord = input.texCoord;
	return output;
}
