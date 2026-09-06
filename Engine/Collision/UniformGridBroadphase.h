/**
 * @file UniformGridBroadphase.h
 * @brief 空間を等間隔のセルに切って候補の組を絞る Broadphase。
 */
#pragma once

#include "Collision/Broadphase.h"
#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include <cstdint>
#include <span>


namespace fang
{
	class IAllocator;

	/**
	 * @brief 空間を等間隔のセルに切る Broadphase。
	 * @details セル幅は Build のたびに登録された箱の平均の大きさから決まる（人が指定しない）。1 個の箱が
	 *          跨いでよいセル数を 8 で打ち切り、超えた箱は「大きいもの」として別の列に退けて全登録との
	 *          総当たりで組を作る。セルの表は毎フレーム数え上げソートで作り直す。
	 * @threading Initialize / Shutdown / Build はメインスレッドのみ。CollectPairs / QueryAabb は const で、
	 *            Build が戻った後なら複数のジョブから同時に呼んでよい。
	 */
	class UniformGridBroadphase final : public IBroadphase
	{
	public:
		FANG_NON_COPYABLE(UniformGridBroadphase);
		FANG_NON_MOVABLE(UniformGridBroadphase);

		UniformGridBroadphase() = default;
		~UniformGridBroadphase() override;

		/** @brief 人が読む名前。常に "UniformGrid"。 */
		[[nodiscard]] const char* GetName() const override { return "UniformGrid"; }


	public:
		[[nodiscard]] bool Initialize(IAllocator& allocator, uint32_t maxColliderCount) override;
		void               Shutdown() override;
		void               Build(std::span<const Aabb> bounds) override;

		[[nodiscard]] uint32_t CollectPairs(std::span<ColliderPair> outPairs) const override;
		[[nodiscard]] uint32_t QueryAabb(const Aabb& bounds, std::span<uint32_t> outIndices) const override;


	private:
		/** @brief 1 個の箱が跨いでよいセル数。超えたら「大きいもの」として別に扱う。 */
		static constexpr uint32_t MAX_CELLS_PER_BOX = 8;

		/** @brief cellSize の下限。1 = 1cm(ADR-025)。箱が全部 1 点でも 0 割りにならない。 */
		static constexpr float MINIMUM_CELL_SIZE = 1.0f;

		/** @brief 箱ごとのセル範囲(floor(座標 / cellSize))。大きいものには持たせない。 */
		struct CellRange
		{
			int32_t minX = 0;
			int32_t minY = 0;
			int32_t minZ = 0;
			int32_t maxX = 0;
			int32_t maxY = 0;
			int32_t maxZ = 0;
		};

		/** @brief セルの表の 1 入り口。数え上げソートでバケット順に並べる。 */
		struct GridEntry
		{
			uint64_t cellKey  = 0;
			uint32_t boxIndex = 0;
		};

		/** @brief セルの鍵をバケット番号へ落とす(Fibonacci ハッシュの上位ビット)。 */
		[[nodiscard]] uint32_t ComputeBucketIndex(uint64_t cellKey) const;


	private:
		IAllocator* m_allocator = nullptr; /**< 借用。Shutdown で返すときにも同じものを使う。 */

		Aabb*      m_bounds     = nullptr; /**< そのフレームの箱。 */
		CellRange* m_cellRanges = nullptr; /**< 箱ごとのセル範囲。大きいものは書かない。 */

		bool*     m_isLarge      = nullptr; /**< 箱ごとに「大きいもの」かどうかの旗。 */
		uint32_t* m_largeIndices = nullptr; /**< 「大きいもの」の番号の列。 */

		GridEntry* m_entries = nullptr; /**< バケット順に並んだセルの入り口。 */

		uint32_t* m_bucketOffsets = nullptr; /**< バケットごとの開始位置。要素数は bucketCount + 1。 */
		uint32_t* m_bucketCursor  = nullptr; /**< 数え上げソートの書き込みカーソル。要素数は bucketCount。 */

		uint32_t m_capacity    = 0;
		uint32_t m_bucketCount = 0; /**< 2 のべき乗。maxColliderCount の 2 倍以上。 */
		uint32_t m_bucketShift = 0; /**< ハッシュの上位ビットを取り出すためのシフト量。 */

		uint32_t m_colliderCount = 0;
		uint32_t m_largeCount    = 0;
		uint32_t m_entryCount    = 0;

		float m_cellSize = MINIMUM_CELL_SIZE; /**< 直近の Build で決めたセル幅。 */
	};
} // namespace fang
