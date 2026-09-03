/**
 * @file Terrain.hlsli
 * @brief 地形の頂点シェーダーとピクセルシェーダーで共有する型。
 */

/**
 * @brief 頂点シェーダーへの入力。
 * @details 並びは TerrainRenderer が private に持つ頂点構造体との契約。片方だけ変えない。
 *          位置はワールド座標で生成済み（地形はワールド行列を持たない）。C++ 側の格納は normal が
 *          8 bit SNORM で、入力アセンブラがここの float へ展開する。UV は持たず、ピクセルシェーダーが
 *          ワールド座標から作る。
 */
struct VertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
};

/** @brief 頂点シェーダーからピクセルシェーダーへ渡すもの。 */
struct VertexOutput
{
	float4 position      : SV_POSITION;
	float3 normal        : NORMAL;    /**< ワールド空間。量子化のずれは PS 側の normalize が受け皿。 */
	float3 worldPosition : TEXCOORD0; /**< スプラット UV・レイヤ UV・ライティングの元。 */
};
