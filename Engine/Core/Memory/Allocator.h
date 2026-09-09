/**
 * @file Allocator.h
 * @brief アロケータのインターフェースと、確保ヘッダの読み書き。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <cstddef>
#include <cstdint>


namespace fang
{
	/** @brief アロケータの使われ具合。 */
	struct AllocatorStatistics
	{
		uint64_t usedBytes;            /**< 今生きている量。 */
		uint64_t peakBytes;            /**< 使用量の最高水位。 */
		uint64_t liveAllocationCount;  /**< 今生きている件数。 */
		uint64_t totalAllocationCount; /**< 起動からの累計。フレームの中の差分を見る検査に使う。 */
	};

	/**
	 * @brief アロケータのインターフェース。
	 * @details メモリはすべてこの実装の中から取る。
	 *          Deallocate で本当に返すアロケータは、利用者ポインタの直前に AllocationHeader を置くこと。
	 *          置かないアロケータ（フレームアロケータのように解放を捨てるもの）は、Deallocate を何もしない実装にする。
	 * @threading 実装ごとに違う。派生クラスの @threading を見ること。
	 */
	class IAllocator
	{
	public:
		FANG_NON_COPYABLE(IAllocator);
		FANG_NON_MOVABLE(IAllocator);

		/** @brief 既定の境界。x64 の MSVC が既定の new に使う境界と同じ。 */
		static constexpr size_t DEFAULT_ALIGNMENT = 16;

		/** @brief 既定のヒープを定数初期化できるように constexpr にしてある。 */
		constexpr IAllocator() = default;
		virtual ~IAllocator()  = default;


	public:
		/** @brief 人が読む名前。ログとパネルに出す。 */
		[[nodiscard]] virtual const char* GetName() const = 0;

		/**
		 * @brief 確保する。
		 * @param size      利用者が使えるバイト数。
		 * @param alignment 返すアドレスの境界。2 のべき乗であること。
		 * @return 利用者ポインタ。失敗したら nullptr。
		 */
		[[nodiscard]] virtual void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) = 0;

		/**
		 * @brief 解放する。
		 * @param memory Allocate が返したポインタ。nullptr を渡してよい（何もしない）。
		 */
		virtual void Deallocate(void* memory) = 0;

		/** @brief 使われ具合を読む。 */
		[[nodiscard]] virtual AllocatorStatistics GetStatistics() const = 0;
	};

	/**
	 * @brief 利用者ポインタの直前に置く、確保 1 件の記録。
	 * @details これがあるので delete と std::unique_ptr がアロケータを知らずに済む。
	 *          大きさを 16 バイトに固定してあり、境界 16 の確保では詰め物が 1 バイトも出ない。
	 */
	struct AllocationHeader
	{
		IAllocator* allocator;     /**< 返す先。 */
		uint32_t    size;          /**< 利用者が頼んだバイト数。 */
		uint16_t    offsetToBlock; /**< 利用者ポインタからブロック先頭までの距離。 */
		uint16_t    magic;         /**< ヘッダの無いポインタを弾くための印。 */
	};
	static_assert(sizeof(AllocationHeader) == 16, "ヘッダは 16 バイトちょうどに収める");

	/** @brief ヘッダが本物かを見分ける印。 */
	inline constexpr uint16_t ALLOCATION_HEADER_MAGIC = 0xFA16;

	/** @brief 頼める境界の上限。ブロック先頭までの距離を 16 ビットに収めるため。 */
	inline constexpr size_t MAXIMUM_ALLOCATION_ALIGNMENT = 32768;

	/** @brief 1 件で頼めるバイト数の上限。ヘッダの size を 32 ビットに収めるため。 */
	inline constexpr size_t MAXIMUM_ALLOCATION_SIZE = 0xFFFFFFFFu;

	/**
	 * @brief ブロック先頭から利用者ポインタまでの距離。
	 * @details ヘッダの大きさを境界の倍数へ切り上げた値。境界 16 なら 16 のまま。
	 */
	[[nodiscard]] constexpr size_t GetAllocationPrefixSize(size_t alignment)
	{
		return (sizeof(AllocationHeader) + alignment - 1) & ~(alignment - 1);
	}

	/**
	 * @brief 確保したブロックにヘッダを書いて、利用者ポインタを返す。
	 * @param block     アロケータが取ったブロックの先頭。境界 alignment に載っていること。
	 * @param allocator 取り出し元。Deallocate はここへ戻る。
	 * @param size      利用者が頼んだバイト数。
	 * @param alignment 利用者ポインタの境界。
	 * @threading 任意のスレッド。書き込むのは自分が取ったブロックの中だけ。
	 */
	[[nodiscard]] void* WriteAllocationHeader(void* block, IAllocator& allocator, size_t size, size_t alignment);

	/**
	 * @brief 利用者ポインタの直前からヘッダを読む。
	 * @param userPointer WriteAllocationHeader が返したポインタ。nullptr を渡さないこと。
	 * @details 印が合わなければ致命的エラーで止める。全構成で見る。
	 *          印が合わないのは、ヘッダの無いメモリを delete したか、ポインタが壊れているとき。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] const AllocationHeader& ReadAllocationHeader(const void* userPointer);

	/**
	 * @brief ヘッダを見て、確保元のアロケータへ返す。
	 * @param userPointer nullptr を渡してよい（何もしない）。
	 * @details グローバルの operator delete がこれを呼ぶ。
	 * @threading 任意のスレッド。
	 */
	void ReturnToAllocator(void* userPointer);
} // namespace fang
