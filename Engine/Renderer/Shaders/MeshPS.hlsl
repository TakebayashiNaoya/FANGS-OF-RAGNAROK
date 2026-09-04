/**
 * @file MeshPS.hlsl
 * @brief メッシュのピクセルシェーダー。平行光 1 本 + 環境項の物理ベースライティング。
 * @details 式と影係数は Lighting.hlsli にある（地形と共有する）。ここはリソースを差して呼ぶだけ。
 *          静的メッシュとスキンメッシュの両方がこれを使う。
 */
#include "Mesh.hlsli"
#include "MeshConstants.h"
#include "Lighting.hlsli"
#include "NormalMapping.hlsli"

/** @brief 描くもの 1 個ぶんの定数。並びは MeshConstants.h の MeshObjectConstants。 */
cbuffer cbObject : register(b0)
{
	MeshObjectConstants objectConstants;
};

/** @brief フレームの間ずっと同じ定数。並びは MeshConstants.h の MeshFrameConstants。 */
cbuffer cbFrame : register(b1)
{
	MeshFrameConstants frameConstants;
};

/** @brief ベースカラー。sRGB の SRV なので、読んだ時点で GPU がリニアへ直している。 */
Texture2D<float4> baseColorTexture : register(t0);
SamplerState baseColorSampler : register(s0);

/** @brief 法線マップ。接線空間で、RG だけを読む。無いときは平坦な 1×1 のダミーが差さる。 */
Texture2D<float4> normalMapTexture : register(t1);

/** @brief シャドウマップ。t はテクスチャ 2 枚の次の枠。深度専用パスが光の視点で書いたもの。 */
Texture2D<float> shadowMap : register(t2);

/** @brief シャドウマップの比較サンプラ。LESS_EQUAL・境界色 白（マップの外は影なしとして読む）。 */
SamplerComparisonState shadowComparisonSampler : register(s1);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	float3 albedo = baseColorTexture.Sample(baseColorSampler, input.texCoord).rgb;

	// 面の向きだけを頂点法線から法線マップへ差し替える。ライティングの式そのものは変えない。
	float2 encodedNormal = normalMapTexture.Sample(baseColorSampler, input.texCoord).rg;
	float3 tangentSpaceNormal = DecodeTangentSpaceNormal(encodedNormal, objectConstants.material.z);
	float3 surfaceNormal = ApplyNormalMap(input.normal, input.tangent, tangentSpaceNormal);

	float3 lighting = CalculateSurfaceLighting(
		albedo,
		objectConstants.material.x,
		objectConstants.material.y,
		surfaceNormal,
		input.worldPosition,
		frameConstants.cameraPosition.xyz,
		frameConstants.directionToLight.xyz,
		frameConstants.lightColor,
		frameConstants.ambientColor.rgb,
		frameConstants.lightViewProjection,
		frameConstants.shadowParameters,
		shadowMap,
		shadowComparisonSampler);

	// ここまではリニア空間。バックバッファは UNORM（sRGB でない）なので、最後にガンマへ戻す。
	// HDR / トーンマップのトピックで、UI 側の補正と一緒にこの pow を消すこと。残すと二重に掛かって白っぽくなる。
	return float4(pow(saturate(lighting), 1.0 / 2.2), 1.0);
}
