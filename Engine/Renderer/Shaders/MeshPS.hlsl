// MeshPS.hlsl
// メッシュのピクセルシェーダー。単色に、法線で明暗だけ付けて返す。
#include "Mesh.hlsli"

// 面から光の来る向き。正規化済み。ライティングの仕組みは別の話なので、形が読める程度に固定で持つ。
static const float3 LIGHT_DIRECTION = float3(0.309, 0.722, -0.619);

static const float3 BASE_COLOR = float3(0.78, 0.76, 0.72);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
	// ワールド行列を渡していないので法線はモデル座標のまま。明暗が物体と一緒に回るが、形を見るには足りる。
	float3 normal = normalize(input.normal);

	// 内積を 0〜1 に写す（半ランバート）。裏側も真っ黒にならず、光源を置かなくても輪郭が読める。
	float brightness = dot(normal, LIGHT_DIRECTION) * 0.5 + 0.5;

	return float4(BASE_COLOR * brightness, 1.0);
}
