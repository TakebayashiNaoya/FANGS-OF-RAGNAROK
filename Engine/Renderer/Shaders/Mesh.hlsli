/**
 * @file Mesh.hlsli
 * @brief メッシュの頂点シェーダーとピクセルシェーダーで共有する型。
 */

/**
 * @brief 頂点シェーダーへの入力。
 * @details 並びは MeshRenderer が private に持つ頂点構造体との契約。片方だけ変えない。
 *          C++ 側の格納は normal が 8 bit SNORM、texCoord が half で、入力アセンブラがここの float へ展開する。
 *          量子化で normal の長さは 1 からわずかにずれる ➡ MeshPS の normalize が受け皿。
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
