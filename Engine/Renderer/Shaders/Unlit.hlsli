/**
 * @file Unlit.hlsli
 * @brief 頂点色を非ライティングで出すシェーダーで共有する型。
 */

/**
 * @brief 頂点シェーダーへの入力。
 * @details 並びは UnlitRenderer が private に持つ頂点構造体との契約。片方だけ変えない。
 */
struct VertexInput
{
	float3 position : POSITION;
	float4 color    : COLOR;
};

/** @brief 頂点シェーダーからピクセルシェーダーへ渡すもの。 */
struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
};
