/**
 * @file MeshVS.hlsl
 * @brief メッシュの頂点シェーダー。ワールドとビュー射影でクリップ座標へ移し、
 *        ライティング用にワールド法線とワールド位置を作る。
 */
#include "Mesh.hlsli"
#include "MeshConstants.h"

/**
 * @brief 描くもの 1 個ぶんの定数。
 * @details C++ 側（Matrix4x4）は行優先ストレージ + 行ベクトル規約で、行列を転置せずそのまま渡してくる。
 *          HLSL の定数バッファは既定で列優先に読むので、ここで転置が掛かって辻褄が合う。
 *          ➡ mul(行列, ベクトル) と「行列が左」で書くと、C++ の p * World * ViewProjection と一致する。
 *          片側だけ流儀を変えると、原因の分からない歪みになる。
 */
cbuffer cbObject : register(b0)
{
	MeshObjectConstants objectConstants;
};

/** @brief フレームの間ずっと同じ定数。並びは MeshConstants.h の MeshFrameConstants。 */
cbuffer cbFrame : register(b1)
{
	MeshFrameConstants frameConstants;
};

VertexOutput VertexMain(VertexInput input)
{
	// ワールド位置はライティングでも使うので、先に作ってクリップ座標へ渡す。
	float4 worldPosition = mul(objectConstants.world, float4(input.position, 1.0));

	VertexOutput output;
	output.position = mul(frameConstants.viewProjection, worldPosition);
	output.worldPosition = worldPosition.xyz;

	// w = 0 で掛けると平行移動が効かない ➡ 法線は向きだけが回る。等倍前提なので逆転置は要らない。
	output.normal = mul(objectConstants.world, float4(input.normal, 0.0)).xyz;

	output.texCoord = input.texCoord;
	return output;
}
