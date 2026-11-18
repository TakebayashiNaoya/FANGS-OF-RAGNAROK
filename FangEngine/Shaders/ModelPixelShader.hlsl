/**
 * @file ModelPixelShader.hlsl
 * @brief モデルのピクセルシェーダー
 */

/**
 * @brief 頂点シェーダーから受け取る構造体
 * @details 頂点シェーダーから受け取る頂点の位置、法線、UV、2つ目のUV、接線を格納する
 */
struct VertexOutput
{
	float4 position     : SV_POSITION;
	float3 worldNormal  : NORMAL;
	float2 uv           : TEXCOORD0;
	float2 secondUv     : TEXCOORD1;
	float4 worldTangent : TANGENT;
};

/**
 * @brief マテリアルの定数バッファ
 * @details メッシュの色の係数を受け取る
 */
cbuffer MaterialConstants : register(b1)
{
	float4 baseColorFactor;
};

/**
 * @brief 光の定数バッファ
 * @details 光の方向、ディレクションライトの色、アンビエントライトの色を受け取る
 */
cbuffer LightConstants : register(b2)
{
	float3 lightDirection;      // 光が進む向き
	float3 directionLightColor; // ディレクションライトの色
	float3 ambientLightColor;   // アンビエントライトの色
};

Texture2D baseColorTexture : register(t0); // メッシュの色のテクスチャ
Texture2D alphaTexture     : register(t1); // メッシュのアルファのテクスチャ
Texture2D normalTexture    : register(t2); // メッシュの法線のテクスチャ

// テクスチャのサンプラー
// 異方性でミップを全段使い、UVは繰り返して取る
SamplerState textureSampler : register(s0);

/**
 * @brief 法線マップを読み、ワールドの法線を出す
 * @param worldNormal 頂点のワールドの法線
 * @param worldTangent 頂点のワールドの接線（wは従法線の向きの符号）
 * @param uv 法線マップを読むUV
 * @return 長さ1のワールドの法線
 */
float3 CalculateWorldNormal(float3 worldNormal, float4 worldTangent, float2 uv)
{
	// 法線マップのxとyを-1～1に戻し、zは長さが1になるように計算する
	float2 normalXY      = normalTexture.Sample(textureSampler, uv).xy * 2.0f - 1.0f;
	float3 tangentNormal = float3(normalXY, sqrt(saturate(1.0f - dot(normalXY, normalXY))));

	// 接線、従法線、法線の3本で、接線空間の法線をワールドへ直す
	float3 normalizedWorldNormal = normalize(worldNormal);
	float3 tangent               = normalize(worldTangent.xyz);
	float3 bitangent             = cross(normalizedWorldNormal, tangent) * worldTangent.w;
	float3 normal                = normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * normalizedWorldNormal);

	return normal;
}

/**
 * @brief 線形空間の色をsRGB空間に変換する
 * @details pow(c, 1/2.2)だと暗い所ほどずれるため、GPUと同じ区分式を使う
 * @param linearColor 線形空間の色
 * @return sRGB空間の色
 */
float3 LinearToSrgb(float3 linearColor)
{
	// 0.0～1.0の範囲に収める
	linearColor = saturate(linearColor);
	float3 low  = linearColor * 12.92f;
	float3 high = 1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f;
	return linearColor <= 0.0031308f ? low : high;
}

float4 main(VertexOutput input) : SV_TARGET
{
	// メッシュの色のテクスチャをサンプリングする
	float4 baseColor = baseColorTexture.Sample(textureSampler, input.uv) * baseColorFactor;
	// アルファのテクスチャをサンプリングする
	float alpha = alphaTexture.Sample(textureSampler, input.secondUv).r;
	// アルファが0.5未満のピクセルは描画しない
	clip(alpha - 0.5f);

	// 法線を正規化して光の方向と内積を取ることで、光の当たり具合を計算する
	float3 normal = CalculateWorldNormal(input.worldNormal, input.worldTangent, input.uv);
	float  diffuse = saturate(dot(normal, -lightDirection));
	float3 color   = baseColor.rgb * (ambientLightColor + directionLightColor * diffuse);

	// sRGB空間に変換して返す
	return float4(LinearToSrgb(color), 1.0f);
}
