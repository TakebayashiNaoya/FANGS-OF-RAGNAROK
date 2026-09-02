// MeshPS.hlsl
// メッシュのピクセルシェーダー。平行光 1 本 + 環境項の物理ベースライティング。
// 式は glTF 仕様 Appendix B（Lambert + GGX + Schlick Fresnel）のとおりで、独自の変形はしない
// ➡ metallic / roughness の係数の意味がアセットと一致する。
// 静的メッシュとスキンメッシュの両方がこれを使う。
#include "Mesh.hlsli"
#include "MeshConstants.h"

static const float PI = 3.14159265;

// 非金属の垂直入射の反射率。glTF 仕様が定める共通の近似値。
static const float DIELECTRIC_REFLECTANCE = 0.04;

cbuffer cbPerObject : register(b0)
{
	MeshObjectConstants constants;
};

// ベースカラー。sRGB の SRV なので、読んだ時点で GPU がリニアへ直している。
Texture2D<float4> baseColorTexture : register(t0);
SamplerState baseColorSampler : register(s0);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	float3 albedo = baseColorTexture.Sample(baseColorSampler, input.texCoord).rgb;

	float metallic = constants.material.x;

	// roughness は知覚値で受け取り、2 乗してから式に入れる（glTF の規約）。
	float alphaRoughness = constants.material.y * constants.material.y;
	float alphaSquared = alphaRoughness * alphaRoughness;

	float3 normal = normalize(input.normal);
	float3 directionToLight = constants.directionToLight.xyz;
	float3 directionToCamera = normalize(constants.cameraPosition.xyz - input.worldPosition);
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

	// 環境項が「光の裏側でも形が読める」役を担う（半ランバートの後継）。metallic で消さないのは、
	// IBL の無い今、金属を真っ黒にしないための近似。
	float3 lighting = (diffuse + specular) * constants.lightColor.rgb * constants.lightColor.w * dotNL
	                + constants.ambientColor.rgb * albedo;

	// ここまではリニア空間。バックバッファは UNORM（sRGB でない）なので、最後にガンマへ戻す。
	// HDR / トーンマップのトピックで、UI 側の補正と一緒にこの pow を消すこと。残すと二重に掛かって白っぽくなる。
	return float4(pow(saturate(lighting), 1.0 / 2.2), 1.0);
}
