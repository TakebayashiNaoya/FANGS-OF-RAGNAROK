/**
 * @file Mesh.hlsli
 * @brief メッシュの頂点シェーダーとピクセルシェーダーで共有する型。
 */

/**
 * @brief 頂点シェーダーへの入力。
 * @details 並びは MeshRenderer が private に持つ頂点構造体との契約。片方だけ変えない。
 *          C++ 側の格納は normal と tangent が 8 bit SNORM、texCoord が half で、入力アセンブラが
 *          ここの float へ展開する。量子化で normal の長さは 1 からわずかにずれる
 *          ➡ NormalMapping.hlsli の normalize が受け皿。
 */
struct VertexInput
{
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 texCoord : TEXCOORD0;
	float4 tangent  : TANGENT; /**< xyz = 接線、w = 従法線の符号（±1）。 */
};

/** @brief 頂点シェーダーからピクセルシェーダーへ渡すもの。 */
struct VertexOutput
{
	float4 position      : SV_POSITION;
	float3 normal        : NORMAL;    /**< ワールド空間。頂点シェーダーが world で回してから渡す。 */
	float2 texCoord      : TEXCOORD0;
	float3 worldPosition : TEXCOORD1; /**< ライティングの視線ベクトル用。 */

	/** @brief xyz = ワールドの接線、w = 従法線の符号。w は補間しても符号が変わらない（頂点ごとに矛盾しないため）。 */
	float4 tangent : TEXCOORD2;
};
