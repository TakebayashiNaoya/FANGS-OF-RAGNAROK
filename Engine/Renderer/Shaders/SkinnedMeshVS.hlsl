/**
 * @file SkinnedMeshVS.hlsl
 * @brief スキンメッシュの頂点シェーダー。骨の姿勢で頂点を変形してから MVP でクリップ座標へ移し、
 *        ライティング用にワールド法線とワールド位置を作る。
 * @details 出力とピクセルシェーダーは静的メッシュと共有する（Mesh.hlsli / MeshPS.hlsl）。
 */
#include "Mesh.hlsli"
#include "MeshConstants.h"

/** @brief 骨の上限。SkinnedMeshRenderer の MAX_JOINT_COUNT と対。片方だけ変えると読み書きの範囲がずれる。 */
#define MAX_JOINT_COUNT 64

/**
 * @brief 頂点シェーダーへの入力。
 * @details 並びは SkinnedMeshRenderer が private に持つ頂点構造体との契約。片方だけ変えない。
 *          C++ 側の格納は normal が 8 bit SNORM、texCoord が half で、入力アセンブラがここの float へ展開する。
 *          量子化で normal の長さは 1 からわずかにずれる ➡ MeshPS の normalize が受け皿。
 */
struct SkinnedVertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
	uint4  joints   : BLENDINDICES;
	float4 weights  : BLENDWEIGHT;
};

/**
 * @brief 描くもの 1 個ぶんの定数。
 * @details C++ 側（Matrix4x4）は行優先ストレージ + 行ベクトル規約で、行列を転置せずそのまま渡してくる。
 *          HLSL の定数バッファは既定で列優先に読むので、ここで転置が掛かって辻褄が合う。
 *          ➡ mul(行列, ベクトル) と「行列が左」で書くと、C++ の合成順と一致する。骨行列も同じ規則。
 */
cbuffer cbPerObject : register(b0)
{
	MeshObjectConstants constants;
};

/** @brief スキニング行列。並びは glTF の関節番号のまま。 */
cbuffer cbSkinning : register(b2)
{
	float4x4 boneMatrices[MAX_JOINT_COUNT];
};

VertexOutput VertexMain(SkinnedVertexInput input)
{
	// 重みは読み込みのときに合計 1 へ正規化してあるので、ここでは割り算をしない。
	float4x4 skinMatrix = boneMatrices[input.joints.x] * input.weights.x
	                    + boneMatrices[input.joints.y] * input.weights.y
	                    + boneMatrices[input.joints.z] * input.weights.z
	                    + boneMatrices[input.joints.w] * input.weights.w;

	float4 skinnedPosition = mul(skinMatrix, float4(input.position, 1.0));

	VertexOutput output;
	output.position = mul(constants.modelViewProjection, skinnedPosition);
	output.worldPosition = mul(constants.world, skinnedPosition).xyz;

	// w = 0 で掛けると平行移動が効かない ➡ 法線は向きだけが回る。骨も world も等倍前提なので逆転置は要らない。
	output.normal = mul(constants.world, mul(skinMatrix, float4(input.normal, 0.0))).xyz;

	output.texCoord = input.texCoord;
	return output;
}
