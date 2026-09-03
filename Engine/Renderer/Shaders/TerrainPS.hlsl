/**
 * @file TerrainPS.hlsl
 * @brief 地形のピクセルシェーダー。スプラットマップの重みで 3 レイヤを混ぜ、メッシュと同じ式で照らす。
 * @details ライティングと影係数は Lighting.hlsli（MeshPS と共有）➡ 建物と光の応答がずれない。
 *          地形は非金属（metallic 0 固定）。影は受けるだけで、落とす側には入らない。
 */
#include "Terrain.hlsli"
#include "MeshConstants.h"
#include "TerrainConstants.h"
#include "Lighting.hlsli"

/** @brief 地形の定数。並びは TerrainConstants.h。読み込みのときに 1 回書かれたきり変わらない。 */
cbuffer cbObject : register(b0)
{
	TerrainConstants terrainConstants;
};

/** @brief フレームの間ずっと同じ定数。シーン View と同じ実体を借りる。 */
cbuffer cbFrame : register(b1)
{
	MeshFrameConstants frameConstants;
};

/** @brief スプラットマップ。RGB が 3 レイヤの重み。数値なので sRGB ではない SRV。 */
Texture2D<float4> splatmapTexture : register(t0);

/** @brief レイヤ 0（草）のアルベド。sRGB の SRV なので、読んだ時点で GPU がリニアへ直している。 */
Texture2D<float4> layerAlbedoTexture0 : register(t1);

/** @brief レイヤ 1（岩）のアルベド。 */
Texture2D<float4> layerAlbedoTexture1 : register(t2);

/** @brief レイヤ 2（土）のアルベド。 */
Texture2D<float4> layerAlbedoTexture2 : register(t3);

/** @brief 共有サンプラ。レイヤのタイリングのため WRAP で作られる（スプラット側は UV をクランプして守る）。 */
SamplerState terrainSampler : register(s0);

/** @brief シャドウマップ。t はテクスチャ 4 枚の次の枠。 */
Texture2D<float> shadowMap : register(t4);

/** @brief シャドウマップの比較サンプラ。LESS_EQUAL・境界色 白（マップの外は影なしとして読む）。 */
SamplerComparisonState shadowComparisonSampler : register(s1);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	float halfWidth = terrainConstants.sizeParameters.x;
	float halfDepth = terrainConstants.sizeParameters.y;
	float splatHalfTexel = terrainConstants.sizeParameters.w;

	// スプラット UV はワールド XZ を 0〜1 へ写したもの。サンプラが WRAP なので、端で反対側の
	// テクセルと混ざらないよう半テクセルぶん内側へクランプする。
	float2 splatUV = float2((input.worldPosition.x + halfWidth) / (2.0 * halfWidth),
	                        (input.worldPosition.z + halfDepth) / (2.0 * halfDepth));
	splatUV = clamp(splatUV, splatHalfTexel, 1.0 - splatHalfTexel);

	// 重みは合計 1 に正規化して使う。全部 0 の画素はレイヤ 0 へフォールバック ➡ 黒い穴を出さない。
	float3 layerWeights = splatmapTexture.Sample(terrainSampler, splatUV).rgb;
	float weightSum = layerWeights.r + layerWeights.g + layerWeights.b;
	layerWeights = weightSum > 0.001 ? layerWeights / weightSum : float3(1.0, 0.0, 0.0);

	// レイヤ UV はワールド座標基準 ➡ 頂点の間隔（画素の間引き）に釣られない。WRAP でタイリングする。
	float2 layerUV = input.worldPosition.xz * terrainConstants.sizeParameters.z;

	float3 albedo = layerAlbedoTexture0.Sample(terrainSampler, layerUV).rgb * layerWeights.r
	              + layerAlbedoTexture1.Sample(terrainSampler, layerUV).rgb * layerWeights.g
	              + layerAlbedoTexture2.Sample(terrainSampler, layerUV).rgb * layerWeights.b;

	float perceptualRoughness = dot(terrainConstants.layerRoughness.xyz, layerWeights);

	// metallic は 0 固定。地面が金属になる状況が無いので、スプラットに 4 チャンネル目を足さない。
	float3 lighting = CalculateSurfaceLighting(
		albedo,
		0.0,
		perceptualRoughness,
		input.normal,
		input.worldPosition,
		frameConstants.cameraPosition.xyz,
		frameConstants.directionToLight.xyz,
		frameConstants.lightColor,
		frameConstants.ambientColor.rgb,
		frameConstants.lightViewProjection,
		frameConstants.shadowParameters,
		shadowMap,
		shadowComparisonSampler);

	// ここまではリニア空間。バックバッファは UNORM（sRGB でない）なので、最後にガンマへ戻す（MeshPS と同じ）。
	return float4(pow(saturate(lighting), 1.0 / 2.2), 1.0);
}
