/**
 * @file UniformGridBroadphase.cpp
 * @brief 等間隔セルへの登録と、セルの表を使った候補の組の絞り込み。
 */
#include "Pch.h"
#include "Collision/UniformGridBroadphase.h"
#include "Collision/CollisionLog.h"
#include "Collision/CollisionMath.h"
#include "Core/Memory/Allocator.h"
#include <algorithm>
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief セル番号の絶対値の上限。cellSize の下限が 1.0 なので、±10^6 の入力でも丸めが効くのはここだけ。 */
		constexpr int32_t CELL_COORDINATE_LIMIT = 1 << 20;

		constexpr uint32_t CELL_KEY_BITS_PER_AXIS = 21;
		constexpr uint64_t CELL_KEY_AXIS_MASK     = (1ull << CELL_KEY_BITS_PER_AXIS) - 1;

		/** @brief バケットへ振るときの黄金比のハッシュ定数。 */
		constexpr uint64_t FIBONACCI_HASH_MULTIPLIER = 0x9E3779B97F4A7C15ull;


		/** @brief 箱ごとの「いちばん長い辺」。cellSize を決める平均に使う。 */
		float ComputeLongestEdge(const Aabb& bounds)
		{
			const Vector3 size = bounds.max - bounds.min;

			return std::max(size.x, std::max(size.y, size.z));
		}


		/** @brief 座標をセル番号へ。±2^20 で丸める。 */
		int32_t ComputeCellCoordinate(float position, float cellSize)
		{
			const float raw = std::floor(position / cellSize);
			if (raw <= static_cast<float>(-CELL_COORDINATE_LIMIT))
			{
				return -CELL_COORDINATE_LIMIT;
			}

			if (raw >= static_cast<float>(CELL_COORDINATE_LIMIT - 1))
			{
				return CELL_COORDINATE_LIMIT - 1;
			}

			return static_cast<int32_t>(raw);
		}


		/** @brief 3 軸のセル番号を 21 ビットずつ詰めた鍵にする。 */
		uint64_t PackCellKey(int32_t x, int32_t y, int32_t z)
		{
			const uint64_t ux = static_cast<uint64_t>(x + CELL_COORDINATE_LIMIT);
			const uint64_t uy = static_cast<uint64_t>(y + CELL_COORDINATE_LIMIT);
			const uint64_t uz = static_cast<uint64_t>(z + CELL_COORDINATE_LIMIT);

			return (ux & CELL_KEY_AXIS_MASK) | ((uy & CELL_KEY_AXIS_MASK) << CELL_KEY_BITS_PER_AXIS) |
				   ((uz & CELL_KEY_AXIS_MASK) << (CELL_KEY_BITS_PER_AXIS * 2));
		}


		/** @brief PackCellKey の逆。共通の最小セルと今見ているセルを比べるために戻す。 */
		void UnpackCellKey(uint64_t key, int32_t* outX, int32_t* outY, int32_t* outZ)
		{
			*outX = static_cast<int32_t>(key & CELL_KEY_AXIS_MASK) - CELL_COORDINATE_LIMIT;
			*outY = static_cast<int32_t>((key >> CELL_KEY_BITS_PER_AXIS) & CELL_KEY_AXIS_MASK) - CELL_COORDINATE_LIMIT;
			*outZ = static_cast<int32_t>((key >> (CELL_KEY_BITS_PER_AXIS * 2)) & CELL_KEY_AXIS_MASK) -
					CELL_COORDINATE_LIMIT;
		}


		/** @brief maxColliderCount の 2 倍以上になる、いちばん小さい 2 のべき乗。 */
		uint32_t ComputeBucketCount(uint32_t maxColliderCount)
		{
			uint32_t bucketCount = 16;
			while (bucketCount < maxColliderCount * 2)
			{
				bucketCount <<= 1;
			}

			return bucketCount;
		}


		/** @brief 2 のべき乗の log2。ハッシュの上位ビットを取り出すシフト量を決めるのに使う。 */
		uint32_t ComputeLog2OfPowerOfTwo(uint32_t value)
		{
			uint32_t result = 0;
			while ((value >> result) > 1)
			{
				++result;
			}

			return result;
		}
	} // namespace


	UniformGridBroadphase::~UniformGridBroadphase()
	{
		Shutdown();
	}


	uint32_t UniformGridBroadphase::ComputeBucketIndex(uint64_t cellKey) const
	{
		const uint64_t hashed = cellKey * FIBONACCI_HASH_MULTIPLIER;

		return static_cast<uint32_t>(hashed >> m_bucketShift);
	}


	bool UniformGridBroadphase::Initialize(IAllocator& allocator, uint32_t maxColliderCount)
	{
		FANG_ASSERT(m_allocator == nullptr, "二重に初期化しようとしている");

		if (maxColliderCount == 0)
		{
			FANG_LOG_ERROR(Collision, "Broadphase の上限が 0 だ");
			return false;
		}

		const uint32_t bucketCount = ComputeBucketCount(maxColliderCount);

		m_bounds        = NewArray<Aabb>(allocator, maxColliderCount);
		m_cellRanges    = NewArray<CellRange>(allocator, maxColliderCount);
		m_isLarge       = NewArray<bool>(allocator, maxColliderCount);
		m_largeIndices  = NewArray<uint32_t>(allocator, maxColliderCount);
		m_entries       = NewArray<GridEntry>(allocator, static_cast<size_t>(maxColliderCount) * MAX_CELLS_PER_BOX);
		m_bucketOffsets = NewArray<uint32_t>(allocator, static_cast<size_t>(bucketCount) + 1);
		m_bucketCursor  = NewArray<uint32_t>(allocator, bucketCount);

		const bool hasAllBuffers = m_bounds != nullptr && m_cellRanges != nullptr && m_isLarge != nullptr &&
								   m_largeIndices != nullptr && m_entries != nullptr && m_bucketOffsets != nullptr &&
								   m_bucketCursor != nullptr;
		if (!hasAllBuffers)
		{
			FANG_LOG_ERROR(Collision, "UniformGridBroadphase の置き場を確保できなかった: {} 個", maxColliderCount);

			DeleteArray(allocator, m_bucketCursor, bucketCount);
			DeleteArray(allocator, m_bucketOffsets, static_cast<size_t>(bucketCount) + 1);
			DeleteArray(allocator, m_entries, static_cast<size_t>(maxColliderCount) * MAX_CELLS_PER_BOX);
			DeleteArray(allocator, m_largeIndices, maxColliderCount);
			DeleteArray(allocator, m_isLarge, maxColliderCount);
			DeleteArray(allocator, m_cellRanges, maxColliderCount);
			DeleteArray(allocator, m_bounds, maxColliderCount);

			m_bucketCursor  = nullptr;
			m_bucketOffsets = nullptr;
			m_entries       = nullptr;
			m_largeIndices  = nullptr;
			m_isLarge       = nullptr;
			m_cellRanges    = nullptr;
			m_bounds        = nullptr;
			return false;
		}

		m_allocator     = &allocator;
		m_capacity      = maxColliderCount;
		m_bucketCount   = bucketCount;
		m_bucketShift   = 64 - ComputeLog2OfPowerOfTwo(bucketCount);
		m_colliderCount = 0;
		m_largeCount    = 0;
		m_entryCount    = 0;
		m_cellSize      = MINIMUM_CELL_SIZE;

		return true;
	}


	void UniformGridBroadphase::Shutdown()
	{
		if (m_allocator == nullptr)
		{
			return;
		}

		DeleteArray(*m_allocator, m_bucketCursor, m_bucketCount);
		DeleteArray(*m_allocator, m_bucketOffsets, static_cast<size_t>(m_bucketCount) + 1);
		DeleteArray(*m_allocator, m_entries, static_cast<size_t>(m_capacity) * MAX_CELLS_PER_BOX);
		DeleteArray(*m_allocator, m_largeIndices, m_capacity);
		DeleteArray(*m_allocator, m_isLarge, m_capacity);
		DeleteArray(*m_allocator, m_cellRanges, m_capacity);
		DeleteArray(*m_allocator, m_bounds, m_capacity);

		m_bucketCursor  = nullptr;
		m_bucketOffsets = nullptr;
		m_entries       = nullptr;
		m_largeIndices  = nullptr;
		m_isLarge       = nullptr;
		m_cellRanges    = nullptr;
		m_bounds        = nullptr;

		m_allocator     = nullptr;
		m_capacity      = 0;
		m_bucketCount   = 0;
		m_bucketShift   = 0;
		m_colliderCount = 0;
		m_largeCount    = 0;
		m_entryCount    = 0;
	}


	void UniformGridBroadphase::Build(std::span<const Aabb> bounds)
	{
		FANG_ASSERT(m_allocator != nullptr, "初期化していない Broadphase に箱を渡そうとしている");

		const uint32_t requestedCount = static_cast<uint32_t>(bounds.size());
		const uint32_t acceptedCount  = (requestedCount < m_capacity) ? requestedCount : m_capacity;
		if (requestedCount > acceptedCount)
		{
			FANG_LOG_WARNING(
				Collision,
				"Broadphase の上限を超えた分を捨てた: {} 個中 {} 個",
				requestedCount,
				acceptedCount
			);
		}

		for (uint32_t index = 0; index < acceptedCount; ++index)
		{
			m_bounds[index] = bounds[index];
		}

		m_colliderCount = acceptedCount;
		m_largeCount    = 0;
		m_entryCount    = 0;

		if (m_colliderCount == 0)
		{
			return;
		}

		// セル幅は登録された箱の「いちばん長い辺」の平均の 2 倍。下限は 1.0(1cm)。
		float totalLongestEdge = 0.0f;
		for (uint32_t index = 0; index < m_colliderCount; ++index)
		{
			totalLongestEdge += ComputeLongestEdge(m_bounds[index]);
		}

		const float averageLongestEdge = totalLongestEdge / static_cast<float>(m_colliderCount);
		m_cellSize                     = std::max(MINIMUM_CELL_SIZE, averageLongestEdge * 2.0f);

		for (uint32_t bucket = 0; bucket < m_bucketCount; ++bucket)
		{
			m_bucketOffsets[bucket] = 0;
		}

		// 1 パス目: セル範囲を決め、大きいものを退けつつ、通常の箱はバケットごとの個数を数える。
		for (uint32_t index = 0; index < m_colliderCount; ++index)
		{
			const Aabb& box = m_bounds[index];

			CellRange range;
			range.minX = ComputeCellCoordinate(box.min.x, m_cellSize);
			range.minY = ComputeCellCoordinate(box.min.y, m_cellSize);
			range.minZ = ComputeCellCoordinate(box.min.z, m_cellSize);
			range.maxX = ComputeCellCoordinate(box.max.x, m_cellSize);
			range.maxY = ComputeCellCoordinate(box.max.y, m_cellSize);
			range.maxZ = ComputeCellCoordinate(box.max.z, m_cellSize);

			const uint64_t spanX         = static_cast<uint64_t>(range.maxX - range.minX + 1);
			const uint64_t spanY         = static_cast<uint64_t>(range.maxY - range.minY + 1);
			const uint64_t spanZ         = static_cast<uint64_t>(range.maxZ - range.minZ + 1);
			const uint64_t spanCellCount = spanX * spanY * spanZ;

			if (spanCellCount > MAX_CELLS_PER_BOX)
			{
				m_isLarge[index]             = true;
				m_largeIndices[m_largeCount] = index;
				++m_largeCount;
				continue;
			}

			m_isLarge[index]    = false;
			m_cellRanges[index] = range;

			for (int32_t z = range.minZ; z <= range.maxZ; ++z)
			{
				for (int32_t y = range.minY; y <= range.maxY; ++y)
				{
					for (int32_t x = range.minX; x <= range.maxX; ++x)
					{
						++m_bucketOffsets[ComputeBucketIndex(PackCellKey(x, y, z))];
					}
				}
			}
		}

		// 2 パス目: 個数から開始位置へ変換(累積和)。カーソルは開始位置の写し。
		uint32_t runningTotal = 0;
		for (uint32_t bucket = 0; bucket < m_bucketCount; ++bucket)
		{
			const uint32_t countInBucket = m_bucketOffsets[bucket];
			m_bucketOffsets[bucket]      = runningTotal;
			m_bucketCursor[bucket]       = runningTotal;
			runningTotal += countInBucket;
		}
		m_bucketOffsets[m_bucketCount] = runningTotal;
		m_entryCount                   = runningTotal;

		// 3 パス目: カーソルの位置へ詰める。
		for (uint32_t index = 0; index < m_colliderCount; ++index)
		{
			if (m_isLarge[index])
			{
				continue;
			}

			const CellRange& range = m_cellRanges[index];
			for (int32_t z = range.minZ; z <= range.maxZ; ++z)
			{
				for (int32_t y = range.minY; y <= range.maxY; ++y)
				{
					for (int32_t x = range.minX; x <= range.maxX; ++x)
					{
						const uint64_t key      = PackCellKey(x, y, z);
						const uint32_t bucket   = ComputeBucketIndex(key);
						const uint32_t position = m_bucketCursor[bucket];
						++m_bucketCursor[bucket];

						m_entries[position] = GridEntry{ .cellKey = key, .boxIndex = index };
					}
				}
			}
		}
	}


	uint32_t UniformGridBroadphase::CollectPairs(std::span<ColliderPair> outPairs) const
	{
		if (m_colliderCount == 0)
		{
			return 0;
		}

		uint32_t pairCount = 0;

		// グリッドの中身どうし。バケットが衝突していても鍵を突き合わせて別セルのものを弾く。
		for (uint32_t bucket = 0; bucket < m_bucketCount; ++bucket)
		{
			const uint32_t rangeBegin = m_bucketOffsets[bucket];
			const uint32_t rangeEnd   = m_bucketOffsets[bucket + 1];

			for (uint32_t entryIndexA = rangeBegin; entryIndexA < rangeEnd; ++entryIndexA)
			{
				const GridEntry& entryA = m_entries[entryIndexA];

				for (uint32_t entryIndexB = entryIndexA + 1; entryIndexB < rangeEnd; ++entryIndexB)
				{
					const GridEntry& entryB = m_entries[entryIndexB];
					if (entryA.cellKey != entryB.cellKey)
					{
						continue;
					}

					const uint32_t boxIndexA = entryA.boxIndex;
					const uint32_t boxIndexB = entryB.boxIndex;

					// 同じ組を 2 回拾わないための規則: 今見ているセルが両者のセル範囲の共通部分の
					// 最小セルと一致するときだけ出す。
					const CellRange& rangeA = m_cellRanges[boxIndexA];
					const CellRange& rangeB = m_cellRanges[boxIndexB];

					int32_t cellX = 0;
					int32_t cellY = 0;
					int32_t cellZ = 0;
					UnpackCellKey(entryA.cellKey, &cellX, &cellY, &cellZ);

					if (cellX != std::max(rangeA.minX, rangeB.minX) || cellY != std::max(rangeA.minY, rangeB.minY) ||
						cellZ != std::max(rangeA.minZ, rangeB.minZ))
					{
						continue;
					}

					if (!OverlapsOnAllAxes(m_bounds[boxIndexA], m_bounds[boxIndexB]))
					{
						continue;
					}

					if (pairCount >= outPairs.size())
					{
						FANG_LOG_WARNING(Collision, "候補の組の上限に達したので打ち切った: {} 組", pairCount);
						return pairCount;
					}

					outPairs[pairCount] = (boxIndexA < boxIndexB)
											  ? ColliderPair{ .indexA = boxIndexA, .indexB = boxIndexB }
											  : ColliderPair{ .indexA = boxIndexB, .indexB = boxIndexA };
					++pairCount;
				}
			}
		}

		// 大きいものは全登録との総当たり。大きいもの同士は番号の小さいほうだけが担当する。
		for (uint32_t largeListIndex = 0; largeListIndex < m_largeCount; ++largeListIndex)
		{
			const uint32_t driverIndex = m_largeIndices[largeListIndex];

			for (uint32_t otherIndex = 0; otherIndex < m_colliderCount; ++otherIndex)
			{
				if (otherIndex == driverIndex)
				{
					continue;
				}

				if (m_isLarge[otherIndex] && otherIndex < driverIndex)
				{
					continue;
				}

				if (!OverlapsOnAllAxes(m_bounds[driverIndex], m_bounds[otherIndex]))
				{
					continue;
				}

				if (pairCount >= outPairs.size())
				{
					FANG_LOG_WARNING(Collision, "候補の組の上限に達したので打ち切った: {} 組", pairCount);
					return pairCount;
				}

				outPairs[pairCount] = (driverIndex < otherIndex)
										  ? ColliderPair{ .indexA = driverIndex, .indexB = otherIndex }
										  : ColliderPair{ .indexA = otherIndex, .indexB = driverIndex };
				++pairCount;
			}
		}

		return pairCount;
	}


	uint32_t UniformGridBroadphase::QueryAabb(const Aabb& bounds, std::span<uint32_t> outIndices) const
	{
		uint32_t writtenCount = 0;

		// 大きいものは経路によらず必ず線形に見る。
		for (uint32_t largeListIndex = 0; largeListIndex < m_largeCount; ++largeListIndex)
		{
			const uint32_t index = m_largeIndices[largeListIndex];
			if (!OverlapsOnAllAxes(m_bounds[index], bounds))
			{
				continue;
			}

			if (writtenCount >= outIndices.size())
			{
				FANG_LOG_WARNING(Collision, "領域クエリの書き込み先が足りず打ち切った: {} 件", writtenCount);
				return writtenCount;
			}

			outIndices[writtenCount] = index;
			++writtenCount;
		}

		if (m_colliderCount == 0)
		{
			return writtenCount;
		}

		CellRange queryRange;
		queryRange.minX = ComputeCellCoordinate(bounds.min.x, m_cellSize);
		queryRange.minY = ComputeCellCoordinate(bounds.min.y, m_cellSize);
		queryRange.minZ = ComputeCellCoordinate(bounds.min.z, m_cellSize);
		queryRange.maxX = ComputeCellCoordinate(bounds.max.x, m_cellSize);
		queryRange.maxY = ComputeCellCoordinate(bounds.max.y, m_cellSize);
		queryRange.maxZ = ComputeCellCoordinate(bounds.max.z, m_cellSize);

		const uint64_t spanX         = static_cast<uint64_t>(queryRange.maxX - queryRange.minX + 1);
		const uint64_t spanY         = static_cast<uint64_t>(queryRange.maxY - queryRange.minY + 1);
		const uint64_t spanZ         = static_cast<uint64_t>(queryRange.maxZ - queryRange.minZ + 1);
		const uint64_t spanCellCount = spanX * spanY * spanZ;

		if (spanCellCount > MAX_CELLS_PER_BOX)
		{
			// 問い合わせの箱が広すぎる。通常の箱は全登録の線形走査に切り替える(大きいものは既に見た)。
			for (uint32_t index = 0; index < m_colliderCount; ++index)
			{
				if (m_isLarge[index])
				{
					continue;
				}

				if (!OverlapsOnAllAxes(m_bounds[index], bounds))
				{
					continue;
				}

				if (writtenCount >= outIndices.size())
				{
					FANG_LOG_WARNING(Collision, "領域クエリの書き込み先が足りず打ち切った: {} 件", writtenCount);
					return writtenCount;
				}

				outIndices[writtenCount] = index;
				++writtenCount;
			}

			return writtenCount;
		}

		for (int32_t z = queryRange.minZ; z <= queryRange.maxZ; ++z)
		{
			for (int32_t y = queryRange.minY; y <= queryRange.maxY; ++y)
			{
				for (int32_t x = queryRange.minX; x <= queryRange.maxX; ++x)
				{
					const uint64_t key        = PackCellKey(x, y, z);
					const uint32_t bucket     = ComputeBucketIndex(key);
					const uint32_t rangeBegin = m_bucketOffsets[bucket];
					const uint32_t rangeEnd   = m_bucketOffsets[bucket + 1];

					for (uint32_t entryIndex = rangeBegin; entryIndex < rangeEnd; ++entryIndex)
					{
						const GridEntry& entry = m_entries[entryIndex];
						if (entry.cellKey != key)
						{
							continue;
						}

						const uint32_t candidateIndex = entry.boxIndex;

						// 問い合わせの箱を「セル範囲を持つもう 1 つの箱」と見て、同じ規則で重複を落とす。
						const CellRange& candidateRange = m_cellRanges[candidateIndex];
						if (x != std::max(queryRange.minX, candidateRange.minX) ||
							y != std::max(queryRange.minY, candidateRange.minY) ||
							z != std::max(queryRange.minZ, candidateRange.minZ))
						{
							continue;
						}

						if (!OverlapsOnAllAxes(m_bounds[candidateIndex], bounds))
						{
							continue;
						}

						if (writtenCount >= outIndices.size())
						{
							FANG_LOG_WARNING(
								Collision,
								"領域クエリの書き込み先が足りず打ち切った: {} 件",
								writtenCount
							);
							return writtenCount;
						}

						outIndices[writtenCount] = candidateIndex;
						++writtenCount;
					}
				}
			}
		}

		return writtenCount;
	}
} // namespace fang
