/**
 * @file TangentGeneration.cpp
 * @brief 接線の生成の実装。
 */
#include "Pch.h"
#include "Renderer/TangentGeneration.h"
#include "Core/Log/Assert.h"
#include <cmath>
#include <vector>


namespace fang
{
	namespace
	{
		/** @brief 法線が渡されていないときに使う向き。MeshRenderer の既定法線と同じ。 */
		constexpr Vector3 DEFAULT_NORMAL = { 0.0f, 1.0f, 0.0f };

		/** @brief UV の行列式がこれ以下の三角形は面積 0 とみなして足し込まない。1/det を発散させないため。 */
		constexpr float DEGENERATE_UV_DETERMINANT = 1e-12f;

		/** @brief 直交化したあとの接線がこの長さ以下なら、法線と平行とみなして既定の軸へ逃がす。 */
		constexpr float MINIMUM_TANGENT_LENGTH = 1e-6f;

		/** @brief 逃げ場の軸を選ぶしきい値。法線がこれより X 寄りなら X 軸を避ける。 */
		constexpr float AXIS_SELECTION_THRESHOLD = 0.9f;

		/**
		 * @brief 法線と直交する適当な単位ベクトルを作る。接線が作れなかった頂点の逃げ場。
		 * @param normal 正規化済みの法線。
		 */
		[[nodiscard]] Vector3 MakeFallbackTangent(const Vector3& normal)
		{
			// 法線と平行な軸を選ぶと引き算の結果が 0 になる ➡ 法線から遠いほうの軸を選ぶ。
			const Vector3 axis = std::fabs(normal.x) < AXIS_SELECTION_THRESHOLD ? Vector3{ 1.0f, 0.0f, 0.0f }
																				: Vector3{ 0.0f, 1.0f, 0.0f };

			return Normalize(axis - normal * Dot(normal, axis));
		}
	} // namespace


	void GenerateTangents(
		std::span<const Vector3>  positions,
		std::span<const Vector3>  normals,
		std::span<const Vector2>  texCoords,
		std::span<const uint16_t> indices,
		std::span<Vector4>        outTangents
	)
	{
		FANG_ASSERT(outTangents.size() == positions.size(), "接線の書き込み先が頂点数と合っていない");
		if (outTangents.size() != positions.size())
		{
			return;
		}

		const size_t vertexCount  = positions.size();
		const bool   hasNormals   = normals.size() == vertexCount;
		const bool   hasTexCoords = texCoords.size() == vertexCount;

		// 三角形ごとの寄与を足し込む作業領域。読み込みのときにしか通らないので、ここでのヒープ確保は許す。
		std::vector<Vector3> accumulatedTangents(vertexCount);
		std::vector<Vector3> accumulatedBitangents(vertexCount);

		for (size_t base = 0; hasTexCoords && base + 3 <= indices.size(); base += 3)
		{
			const uint16_t triangleIndices[3] = { indices[base], indices[base + 1], indices[base + 2] };
			if (triangleIndices[0] >= vertexCount || triangleIndices[1] >= vertexCount ||
				triangleIndices[2] >= vertexCount)
			{
				FANG_ASSERT(false, "インデックスが頂点の数を超えている");
				continue;
			}

			const Vector3 edge1 = positions[triangleIndices[1]] - positions[triangleIndices[0]];
			const Vector3 edge2 = positions[triangleIndices[2]] - positions[triangleIndices[0]];

			const float deltaU1 = texCoords[triangleIndices[1]].x - texCoords[triangleIndices[0]].x;
			const float deltaV1 = texCoords[triangleIndices[1]].y - texCoords[triangleIndices[0]].y;
			const float deltaU2 = texCoords[triangleIndices[2]].x - texCoords[triangleIndices[0]].x;
			const float deltaV2 = texCoords[triangleIndices[2]].y - texCoords[triangleIndices[0]].y;

			// UV が 1 本の線に潰れている三角形は、この面から接線の向きを決められない ➡ 足し込まない。
			const float determinant = deltaU1 * deltaV2 - deltaU2 * deltaV1;
			if (std::fabs(determinant) <= DEGENERATE_UV_DETERMINANT)
			{
				continue;
			}

			const float inverseDeterminant = 1.0f / determinant;

			// 位置の差を UV の差で解いたもの。tangent が ∂P/∂u、bitangent が ∂P/∂v。
			const Vector3 tangent   = (edge1 * deltaV2 - edge2 * deltaV1) * inverseDeterminant;
			const Vector3 bitangent = (edge2 * deltaU1 - edge1 * deltaU2) * inverseDeterminant;

			// 面の寄与を 3 頂点へ足す。共有された頂点では、隣り合う面の向きが平均される。
			for (const uint16_t vertexIndex : triangleIndices)
			{
				accumulatedTangents[vertexIndex] += tangent;
				accumulatedBitangents[vertexIndex] += bitangent;
			}
		}

		for (size_t index = 0; index < vertexCount; ++index)
		{
			const Vector3 sourceNormal = hasNormals ? normals[index] : DEFAULT_NORMAL;
			const Vector3 normal       = LengthSquared(sourceNormal) > 0.0f ? Normalize(sourceNormal) : DEFAULT_NORMAL;

			// グラム・シュミットで法線と直交させる。斜交した TBN を渡すと、法線マップの向きが寝たまま回る。
			const Vector3 accumulated = accumulatedTangents[index];

			Vector3     tangent = accumulated - normal * Dot(normal, accumulated);
			const float length  = Length(tangent);

			tangent = length > MINIMUM_TANGENT_LENGTH ? tangent * (1.0f / length) : MakeFallbackTangent(normal);

			// 従法線が ∂P/∂v と逆を向いていたら w を -1 にする ➡ シェーダ側の cross(N, T) * w が ∂P/∂v に一致する。
			const float handedness = Dot(Cross(normal, tangent), accumulatedBitangents[index]) < 0.0f ? -1.0f : 1.0f;

			outTangents[index] = Vector4{ tangent.x, tangent.y, tangent.z, handedness };
		}
	}
} // namespace fang
