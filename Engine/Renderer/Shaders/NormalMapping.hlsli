/**
 * @file NormalMapping.hlsli
 * @brief 法線マップから面の向きを作る。メッシュ（MeshPS）と地形（TerrainPS）の両方が include する。
 * @details ライティングの式には触らず、Lighting.hlsli へ渡す法線を作る手前に 1 段挟むだけ。
 *          接線空間の規約は glTF に合わせて 従法線 = ∂P/∂v ➡ 符号の分岐をシェーダに持ち込まない。
 */

/**
 * @brief 法線マップの RG から接線空間の法線を復元する。
 * @param encodedRedGreen テクスチャから読んだ 0〜1 の 2 成分。
 * @param strength        強さ。glTF の normalTexture.scale と同じ意味で、1.0 が焼いたとおり。
 * @details B は読まない ➡ 2 チャンネルの BC5 と、ダミーの RGBA8 が同じ 1 本のコードを通る。
 */
float3 DecodeTangentSpaceNormal(float2 encodedRedGreen, float strength)
{
	// 0〜1 を -1〜1 へ戻し、強さで xy を伸ばす。z は残りから復元するので、伸ばすほど寝た法線になる。
	float2 tangentSpaceXY = (encodedRedGreen * 2.0 - 1.0) * strength;

	float z = sqrt(saturate(1.0 - dot(tangentSpaceXY, tangentSpaceXY)));

	return float3(tangentSpaceXY, z);
}

/**
 * @brief 接線空間の法線をワールドへ回す。
 * @param vertexNormal      頂点から補間されたままのワールド法線。長さ 1 でなくてよい。
 * @param tangent           xyz = ワールドの接線、w = 従法線の符号（±1）。
 * @param tangentSpaceNormal DecodeTangentSpaceNormal が返したもの。
 */
float3 ApplyNormalMap(float3 vertexNormal, float4 tangent, float3 tangentSpaceNormal)
{
	float3 normal = normalize(vertexNormal);

	// 補間で接線が法線と直交しなくなっているので、ここで直交化してから TBN を組む。
	float3 tangentAxis = normalize(tangent.xyz - normal * dot(normal, tangent.xyz));
	float3 bitangentAxis = cross(normal, tangentAxis) * tangent.w;

	return normalize(tangentSpaceNormal.x * tangentAxis
	               + tangentSpaceNormal.y * bitangentAxis
	               + tangentSpaceNormal.z * normal);
}
