/**
 * @file Mesh.hlsli
 * @brief メッシュの頂点シェーダーとピクセルシェーダーで共有する型。
 */

/**
 * @brief 頂点シェーダーへの入力。
 * @details 並びは MeshRenderer が private に持つ頂点構造体との契約。片方だけ変えない。
 */
struct VertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
};

/** @brief 頂点シェーダーからピクセルシェーダーへ渡すもの。 */
struct VertexOutput
{
	float4 position      : SV_POSITION;
	float3 normal        : NORMAL;    /**< ワールド空間。頂点シェーダーが world で回してから渡す。 */
	float2 texCoord      : TEXCOORD0;
	float3 worldPosition : TEXCOORD1; /**< ライティングの視線ベクトル用。 */
};
