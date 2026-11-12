/**
 * @file ModelPixelShader.hlsl
 * @brief モデルのピクセルシェーダー
 */

/**
 * @brief 頂点シェーダーから受け取る構造体
 * @details 頂点シェーダーから受け取る頂点の位置、法線、UV、2つ目のUVを格納する
 */
struct VertexOutput
{
	float4 position : SV_POSITION;
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD0;
	float2 secondUv : TEXCOORD1;
};

/**
 * @brief マテリアルの定数バッファ
 * @details メッシュの色の係数を受け取る
 */
cbuffer MaterialConstants : register(b1)
{
	float4 baseColorFactor;
};

Texture2D baseColorTexture : register(t0); // メッシュの色のテクスチャ
Texture2D alphaTexture     : register(t1); // メッシュのアルファのテクスチャ

// テクスチャのサンプラー
// 異方性でミップを全段使い、UVは繰り返して取る
SamplerState textureSampler : register(s0);

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
	float  alpha     = alphaTexture.Sample(textureSampler, input.secondUv).r;
	// アルファが0.5未満のピクセルは描画しない
	clip(alpha - 0.5f);
	// sRGB空間に変換して返す
	return float4(LinearToSrgb(baseColor.rgb), 1.0f);
}
