/**
 * @file HeightmapTerrain.cpp
 * @brief ハイトマップからの地形チャンク生成と高さ問い合わせの実装。
 */
#include "Pch.h"
#include "Resource/HeightmapTerrain.h"
#include "Resource/DdsImage.h"
#include "Resource/ResourceLog.h"
#include <algorithm>
#include <cstring>


namespace fang
{
	namespace
	{
		/** @brief 16 bit インデックスで指せる頂点の数。チャンク 1 個の上限。 */
		constexpr uint32_t MAX_CHUNK_VERTEX_COUNT = 65536;

		/** @brief 画素の最大値。R16 UNORM の 65535 がこの値のとき高さは heightScale になる。 */
		constexpr float PIXEL_MAX_VALUE = 65535.0f;
	} // namespace


	bool HeightmapTerrain::Load(const char* filePath, const HeightmapTerrainDesc& desc)
	{
		Clear();

		DdsImage image;
		if (!image.Load(filePath))
		{
			FANG_LOG_ERROR(Resource, "ハイトマップを読めなかった: {}", filePath == nullptr ? "(null)" : filePath);
			return false;
		}

		if (image.GetFormat() != rhi::EnTextureFormat::R16)
		{
			FANG_LOG_ERROR(Resource, "ハイトマップは R16 であること: {}", filePath);
			return false;
		}

		// 使うのは最大解像度の 1 段だけ。ミップが付いていても無視する。
		const rhi::TextureMipLevel& mipLevel = image.GetMipLevels()[0];

		// バイト列から 16 bit の画素列へ写す。行間の詰め物が無いことは DdsImage が保証している。
		std::vector<uint16_t> heights(static_cast<size_t>(mipLevel.width) * mipLevel.height);
		std::memcpy(heights.data(), mipLevel.pixels, heights.size() * sizeof(uint16_t));

		if (!BuildFromHeights(heights, mipLevel.width, mipLevel.height, desc))
		{
			return false;
		}

		FANG_LOG_INFO(Resource, "ハイトマップ地形を読んだ: {}", filePath);

		return true;
	}


	bool HeightmapTerrain::BuildFromHeights(
		std::span<const uint16_t>   heights,
		uint32_t                    pixelCountX,
		uint32_t                    pixelCountZ,
		const HeightmapTerrainDesc& desc
	)
	{
		Clear();

		if (pixelCountX < 2 || pixelCountZ < 2)
		{
			FANG_LOG_ERROR(Resource, "ハイトマップの画素数が足りない: {}x{}", pixelCountX, pixelCountZ);
			return false;
		}

		if (heights.size() != static_cast<size_t>(pixelCountX) * pixelCountZ)
		{
			FANG_LOG_ERROR(
				Resource,
				"高さ配列の要素数が画素数と合っていない: {} と {}x{}",
				heights.size(),
				pixelCountX,
				pixelCountZ
			);
			return false;
		}

		if (desc.totalWidth <= 0.0f || desc.totalDepth <= 0.0f)
		{
			FANG_LOG_ERROR(Resource, "地形の全長が 0 以下だ: {} x {}", desc.totalWidth, desc.totalDepth);
			return false;
		}

		// 65,536 頂点を超えるチャンクを黙って作らない。生成前に条件そのものを弾く。
		const uint32_t chunkVertexCountPerSide = desc.chunkQuadCount + 1;
		if (desc.chunkQuadCount == 0 || chunkVertexCountPerSide * chunkVertexCountPerSide > MAX_CHUNK_VERTEX_COUNT)
		{
			FANG_LOG_ERROR(
				Resource,
				"チャンクのクワッド数が不正だ: {}。1 チャンクの頂点は {} 個まで",
				desc.chunkQuadCount,
				MAX_CHUNK_VERTEX_COUNT
			);
			return false;
		}

		m_heights.assign(heights.begin(), heights.end());
		m_pixelCountX = pixelCountX;
		m_pixelCountZ = pixelCountZ;
		m_desc        = desc;

		m_cellSizeX = desc.totalWidth / static_cast<float>(pixelCountX - 1);
		m_cellSizeZ = desc.totalDepth / static_cast<float>(pixelCountZ - 1);
		m_originX   = -desc.totalWidth * 0.5f;
		m_originZ   = -desc.totalDepth * 0.5f;

		// チャンクは chunkQuadCount 単位の均等分割で、端だけ小さくなる。
		const uint32_t quadCountX  = pixelCountX - 1;
		const uint32_t quadCountZ  = pixelCountZ - 1;
		const uint32_t chunkCountX = (quadCountX + desc.chunkQuadCount - 1) / desc.chunkQuadCount;
		const uint32_t chunkCountZ = (quadCountZ + desc.chunkQuadCount - 1) / desc.chunkQuadCount;

		for (uint32_t chunkZ = 0; chunkZ < chunkCountZ; ++chunkZ)
		{
			for (uint32_t chunkX = 0; chunkX < chunkCountX; ++chunkX)
			{
				BuildChunk(chunkX, chunkZ);
			}
		}

		// span は実体の生成が全部終わってから作る。生成中の vector の伸長で先を失わないため。
		m_chunks.reserve(m_chunkGeometries.size());
		for (const ChunkGeometry& geometry : m_chunkGeometries)
		{
			m_chunks.push_back(
				TerrainChunkSource{
					.positions = geometry.positions,
					.normals   = geometry.normals,
					.indices   = geometry.indices,
					.bounds    = MakeAabbFromPoints(geometry.positions),
				}
			);
		}

		size_t totalVertexCount = 0;
		for (const ChunkGeometry& geometry : m_chunkGeometries)
		{
			totalVertexCount += geometry.positions.size();
		}

		FANG_LOG_INFO(
			Resource,
			"地形チャンクを生成した: {} 個（{}x{} 分割）/ 頂点 {} 個",
			m_chunks.size(),
			chunkCountX,
			chunkCountZ,
			totalVertexCount
		);

		return true;
	}


	float HeightmapTerrain::GetHeightAt(float worldX, float worldZ) const
	{
		if (m_heights.empty())
		{
			return 0.0f;
		}

		// 画素空間へ移し、境界へクランプする。範囲外の問い合わせは端の高さになる。
		const float pixelSpaceX =
			std::clamp((worldX - m_originX) / m_cellSizeX, 0.0f, static_cast<float>(m_pixelCountX - 1));
		const float pixelSpaceZ =
			std::clamp((worldZ - m_originZ) / m_cellSizeZ, 0.0f, static_cast<float>(m_pixelCountZ - 1));

		// 右端・下端ちょうどでも 4 画素が範囲に収まるよう、基準の画素は 1 つ内側までに抑える。
		const uint32_t baseX = std::min(static_cast<uint32_t>(pixelSpaceX), m_pixelCountX - 2);
		const uint32_t baseZ = std::min(static_cast<uint32_t>(pixelSpaceZ), m_pixelCountZ - 2);

		const float fractionX = pixelSpaceX - static_cast<float>(baseX);
		const float fractionZ = pixelSpaceZ - static_cast<float>(baseZ);

		const float height00 = PixelToHeight(baseX, baseZ);
		const float height10 = PixelToHeight(baseX + 1, baseZ);
		const float height01 = PixelToHeight(baseX, baseZ + 1);
		const float height11 = PixelToHeight(baseX + 1, baseZ + 1);

		const float heightNear = height00 + (height10 - height00) * fractionX;
		const float heightFar  = height01 + (height11 - height01) * fractionX;

		return heightNear + (heightFar - heightNear) * fractionZ;
	}


	float HeightmapTerrain::PixelToHeight(uint32_t pixelX, uint32_t pixelZ) const
	{
		const uint16_t pixel = m_heights[static_cast<size_t>(pixelZ) * m_pixelCountX + pixelX];

		return static_cast<float>(pixel) / PIXEL_MAX_VALUE * m_desc.heightScale;
	}


	Vector3 HeightmapTerrain::CalculateNormal(uint32_t pixelX, uint32_t pixelZ) const
	{
		// 中央差分。境界は画素をクランプするので片側差分になる。
		const uint32_t leftX  = pixelX > 0 ? pixelX - 1 : pixelX;
		const uint32_t rightX = pixelX < m_pixelCountX - 1 ? pixelX + 1 : pixelX;
		const uint32_t nearZ  = pixelZ > 0 ? pixelZ - 1 : pixelZ;
		const uint32_t farZ   = pixelZ < m_pixelCountZ - 1 ? pixelZ + 1 : pixelZ;

		const float slopeX = (PixelToHeight(rightX, pixelZ) - PixelToHeight(leftX, pixelZ)) /
							 (static_cast<float>(rightX - leftX) * m_cellSizeX);
		const float slopeZ = (PixelToHeight(pixelX, farZ) - PixelToHeight(pixelX, nearZ)) /
							 (static_cast<float>(farZ - nearZ) * m_cellSizeZ);

		// 高さ場 y = h(x, z) の法線は (-∂h/∂x, 1, -∂h/∂z) に比例する。
		return Normalize(Vector3{ -slopeX, 1.0f, -slopeZ });
	}


	void HeightmapTerrain::BuildChunk(uint32_t chunkX, uint32_t chunkZ)
	{
		const uint32_t quadBeginX = chunkX * m_desc.chunkQuadCount;
		const uint32_t quadBeginZ = chunkZ * m_desc.chunkQuadCount;
		const uint32_t quadEndX   = std::min(quadBeginX + m_desc.chunkQuadCount, m_pixelCountX - 1);
		const uint32_t quadEndZ   = std::min(quadBeginZ + m_desc.chunkQuadCount, m_pixelCountZ - 1);

		// 隣のチャンクと縁の頂点 1 列を重複させて継ぎ目を閉じる。頂点はクワッドより 1 列多い。
		const uint32_t vertexCountX = quadEndX - quadBeginX + 1;
		const uint32_t vertexCountZ = quadEndZ - quadBeginZ + 1;

		ChunkGeometry geometry;
		geometry.positions.reserve(static_cast<size_t>(vertexCountX) * vertexCountZ);
		geometry.normals.reserve(static_cast<size_t>(vertexCountX) * vertexCountZ);

		for (uint32_t localZ = 0; localZ < vertexCountZ; ++localZ)
		{
			for (uint32_t localX = 0; localX < vertexCountX; ++localX)
			{
				const uint32_t pixelX = quadBeginX + localX;
				const uint32_t pixelZ = quadBeginZ + localZ;

				geometry.positions.push_back(
					Vector3{
						m_originX + static_cast<float>(pixelX) * m_cellSizeX,
						PixelToHeight(pixelX, pixelZ),
						m_originZ + static_cast<float>(pixelZ) * m_cellSizeZ,
					}
				);
				geometry.normals.push_back(CalculateNormal(pixelX, pixelZ));
			}
		}

		for (uint32_t localZ = 0; localZ + 1 < vertexCountZ; ++localZ)
		{
			for (uint32_t localX = 0; localX + 1 < vertexCountX; ++localX)
			{
				// 4 頂点すべてが minHeight を下回るクワッドだけインデックスを省く。頂点は残る
				// ➡ 境目のクワッドが隣の頂点を指せる。
				const float height00 = geometry.positions[localZ * vertexCountX + localX].y;
				const float height10 = geometry.positions[localZ * vertexCountX + localX + 1].y;
				const float height01 = geometry.positions[(localZ + 1) * vertexCountX + localX].y;
				const float height11 = geometry.positions[(localZ + 1) * vertexCountX + localX + 1].y;

				if (height00 < m_desc.minHeight && height10 < m_desc.minHeight && height01 < m_desc.minHeight &&
					height11 < m_desc.minHeight)
				{
					continue;
				}

				const uint16_t index00 = static_cast<uint16_t>(localZ * vertexCountX + localX);
				const uint16_t index10 = static_cast<uint16_t>(localZ * vertexCountX + localX + 1);
				const uint16_t index01 = static_cast<uint16_t>((localZ + 1) * vertexCountX + localX);
				const uint16_t index11 = static_cast<uint16_t>((localZ + 1) * vertexCountX + localX + 1);

				// 巻き順は床メッシュと同じ結論（Cross(v1 - v0, v2 - v0) が上向きの法線に一致する順番）。
				geometry.indices.push_back(index00);
				geometry.indices.push_back(index01);
				geometry.indices.push_back(index11);

				geometry.indices.push_back(index00);
				geometry.indices.push_back(index11);
				geometry.indices.push_back(index10);
			}
		}

		// 全クワッドが省かれたチャンクは描くものが無いので列へ入れない。
		if (geometry.indices.empty())
		{
			return;
		}

		m_chunkGeometries.push_back(std::move(geometry));
	}


	void HeightmapTerrain::Clear()
	{
		// span が実体を指しているので、指している側から先に捨てる。
		m_chunks.clear();
		m_chunkGeometries.clear();
		m_heights.clear();

		m_pixelCountX = 0;
		m_pixelCountZ = 0;
	}
} // namespace fang
