/**
 * @file Lighting.hlsli
 * @brief 平行光 1 本 + 環境項の物理ベースライティングと、シャドウマップの影係数。
 * @details 式は glTF 仕様 Appendix B（Lambert + GGX + Schlick Fresnel）のとおりで、独自の変形はしない
 *          ➡ metallic / roughness の係数の意味がアセットと一致する。
 *          メッシュ（MeshPS）と地形（TerrainPS）の両方が include する ➡ 同じ絵を出す式を 2 本持たない。
 *          計算の順序は抽出前の MeshPS と 1 行ずつ同じにしてある。並べ替えると FXC の出すコードが変わり、
 *          抽出の前後で絵が同じことをバイトコードの一致で確かめられなくなる。
 */

/** @brief 円周率。 */
static const float PI = 3.14159265;

/** @brief 非金属の垂直入射の反射率。glTF 仕様が定める共通の近似値。 */
static const float DIELECTRIC_REFLECTANCE = 0.04;

/**
 * @brief ワールド位置が光にどれだけ見えているかを 0（影）〜1（影なし）で返す。
 * @details 3x3 の PCF（9 回サンプル）で境目を滑らかにする。影が無効なフレームと、
 *          光のフラスタムの外（z が 0〜1 の範囲外）は範囲外を不正に暗くしないため 1 を返す。
 */
float CalculateShadowFactor(
	float3 worldPosition,
	float4x4 lightViewProjection,
	float4 shadowParameters,
	Texture2D<float> shadowMap,
	SamplerComparisonState shadowComparisonSampler)
{
	// 早期 return を使うと FXC が「戻り値が初期化されていないかもしれない」という誤検知の警告を出すので、
	// 1 つの戻り値を条件分岐の中で埋めていく形にする。既定値は影なし（範囲外を不正に暗くしないため）。
	float shadowFactor = 1.0;

	if (shadowParameters.y >= 0.5)
	{
		float4 lightClipPosition = mul(lightViewProjection, float4(worldPosition, 1.0));

		// 正射影なので w 除算は実質 1 だが、既存の書き方に合わせて安全に割っておく。
		float3 lightNdcPosition = lightClipPosition.xyz / lightClipPosition.w;

		if (lightNdcPosition.z >= 0.0 && lightNdcPosition.z <= 1.0)
		{
			float2 shadowMapUV = lightNdcPosition.xy * float2(0.5, -0.5) + 0.5;
			float texelSize = shadowParameters.x;

			float shadowSum = 0.0;
			for (int y = -1; y <= 1; ++y)
			{
				for (int x = -1; x <= 1; ++x)
				{
					float2 sampleUV = shadowMapUV + float2(x, y) * texelSize;
					shadowSum += shadowMap.SampleCmpLevelZero(shadowComparisonSampler, sampleUV, lightNdcPosition.z);
				}
			}

			shadowFactor = shadowSum / 9.0;
		}
	}

	return shadowFactor;
}

/**
 * @brief 平行光 1 本 + 環境項 + 影で、面の色をリニア空間で作る。
 * @param vertexNormal 頂点から補間されたままのワールド法線。長さ 1 でなくてよい（この中で正規化する）。
 * @return リニア空間のライティング結果。ガンマへ戻すのは呼び出し側の仕事。
 */
float3 CalculateSurfaceLighting(
	float3 albedo,
	float metallic,
	float perceptualRoughness,
	float3 vertexNormal,
	float3 worldPosition,
	float3 cameraPosition,
	float3 directionToLight,
	float4 lightColor,
	float3 ambientColor,
	float4x4 lightViewProjection,
	float4 shadowParameters,
	Texture2D<float> shadowMap,
	SamplerComparisonState shadowComparisonSampler)
{
	// roughness は知覚値で受け取り、2 乗してから式に入れる（glTF の規約）。
	float alphaRoughness = perceptualRoughness * perceptualRoughness;
	float alphaSquared = alphaRoughness * alphaRoughness;

	float3 normal = normalize(vertexNormal);
	float3 directionToCamera = normalize(cameraPosition - worldPosition);
	float3 halfVector = normalize(directionToLight + directionToCamera);

	float dotNL = saturate(dot(normal, directionToLight));
	float dotNV = saturate(dot(normal, directionToCamera));
	float dotNH = saturate(dot(normal, halfVector));
	float dotVH = saturate(dot(directionToCamera, halfVector));

	// F0 は非金属なら 0.04、金属ならベースカラーそのもの。そのぶん金属は拡散を持たない。
	float3 reflectanceAtZero = lerp(float3(DIELECTRIC_REFLECTANCE, DIELECTRIC_REFLECTANCE, DIELECTRIC_REFLECTANCE),
	                                albedo, metallic);
	float3 fresnel = reflectanceAtZero + (1.0 - reflectanceAtZero) * pow(1.0 - dotVH, 5.0);

	float3 diffuse = (1.0 - fresnel) * albedo * (1.0 - metallic) / PI;

	// GGX の法線分布と、Smith の高さ相関マスキング（可視項）。
	float distributionDenominator = dotNH * dotNH * (alphaSquared - 1.0) + 1.0;
	float distribution = alphaSquared / (PI * distributionDenominator * distributionDenominator);

	float maskingLight = dotNL * sqrt(dotNV * dotNV * (1.0 - alphaSquared) + alphaSquared);
	float maskingCamera = dotNV * sqrt(dotNL * dotNL * (1.0 - alphaSquared) + alphaSquared);
	float visibility = 0.5 / max(maskingLight + maskingCamera, 1e-5);

	float3 specular = distribution * visibility * fresnel;

	// 影は直接光にだけ掛ける。環境項にまで掛けると影の中が真っ黒になり形が読めなくなる。
	float shadowFactor = CalculateShadowFactor(
		worldPosition, lightViewProjection, shadowParameters, shadowMap, shadowComparisonSampler);

	// 環境項が「光の裏側でも形が読める」役を担う（半ランバートの後継）。metallic で消さないのは、
	// IBL の無い今、金属を真っ黒にしないための近似。
	return shadowFactor * (diffuse + specular) * lightColor.rgb * lightColor.w * dotNL
	     + ambientColor * albedo;
}
