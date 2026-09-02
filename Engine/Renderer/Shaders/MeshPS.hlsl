// MeshPS.hlsl
// メッシュのピクセルシェーダー。ベースカラーをサンプルし、法線で明暗だけ付けて返す。
// 静的メッシュとスキンメッシュの両方がこれを使う。
#include "Mesh.hlsli"

// 面から光の来る向き。正規化済み。ライティングの仕組みは別の話なので、形が読める程度に固定で持つ。
static const float3 LIGHT_DIRECTION = float3(0.309, 0.722, -0.619);

// ベースカラー。sRGB の SRV なので、読んだ時点で GPU がリニアへ直している。
Texture2D<float4> baseColorTexture : register(t0);
SamplerState baseColorSampler : register(s0);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	float3 albedo = baseColorTexture.Sample(baseColorSampler, input.texCoord).rgb;

	// ワールド行列を渡していないので法線はモデル座標のまま。明暗が物体と一緒に回るが、形を見るには足りる。
	float3 normal = normalize(input.normal);

	// 内積を 0〜1 に写す（半ランバート）。裏側も真っ黒にならず、光源を置かなくても輪郭が読める。
	float brightness = dot(normal, LIGHT_DIRECTION) * 0.5 + 0.5;

	// ここまではリニア空間。バックバッファは UNORM（sRGB でない）なので、最後にガンマへ戻す。
	// バックバッファを SRGB 形式へ変える日が来たら、この pow は消すこと。残すと二重に掛かって白っぽくなる。
	float3 lit = saturate(albedo * brightness);
	return float4(pow(lit, 1.0 / 2.2), 1.0);
}
