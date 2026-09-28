/**
 * @file StackAllocator.h
 * @brief 先端を進めるだけで確保し、後に取った物から返すアロケータ。
 */
#pragma once

#include "Core/Memory/Allocator.h"
#include <cstddef>
#include <cstdint>
#include <thread>


namespace fang
{
	/**
	 * @brief スタックアロケータの先端の位置を写した値。
	 * @details メモリは取らない。
	 *          FreeToMarker に渡すと、この位置まで先端を戻す。
	 *          件数も一緒に写すのは、先端だけ戻すと IsEmpty と統計が狂うため。
	 */
	struct StackMarker
	{
		size_t   top;                 /**< 印を付けたときの先端（領域の先頭からのバイト数）。 */
		uint64_t liveAllocationCount; /**< 印を付けたときの生きている確保の件数。 */
	};

	/**
	 * @brief 先端を進めるだけで確保し、後に取った物から返すアロケータ。
	 * @details 作るときに取り出し元から 1 塊だけ取り、以後はその中で先端を進める。
	 *          寿命が入れ子になるデータ（1 ランの間ずっと要る物の上に、1 ステージの間だけ要る物）を断片化させずに置くためのもの。
	 *          確保のたびに利用者ポインタの直前へ確保ヘッダを書くので、new (stack) T(...) で作った物を delete p で返せる。
	 *          返すのは後に取った物からに限る。
	 *          順番を破ると全構成で止まる。
	 *          印まで戻しても FreeAll してもデストラクタは呼ばないので、後始末の要らないデータだけを置く。
	 *          使用量は占めている量で数える。
	 *          スタックでは先端の位置と同じ値になり、容量とそのまま比べられる。
	 * @threading 作ったスレッドのみ。
	 *            錠は持たない。
	 *            他のスレッドから呼ぶと Debug / Preview でアサート。
	 */
	class StackAllocator final : public IAllocator
	{
	public:
		FANG_NON_COPYABLE(StackAllocator);
		FANG_NON_MOVABLE(StackAllocator);


	public:
		/**
		 * @brief 取り出し元から領域を 1 塊取って作る。
		 * @param name     ログに出す名前。
		 *                 寿命を持たないので文字列リテラルを渡すこと。
		 * @param source   取り出し元。
		 *                 このスタックより長生きすること。
		 * @param capacity 領域のバイト数（確保ヘッダと詰め物もここから使う）。
		 *                 0 は不可。
		 * @details 領域を取れなければ致命的エラーで止まる。
		 *          持ち主のクラスのメンバか関数の中に置き、生きている確保を全部返してから壊すこと。
		 */
		StackAllocator(const char* name, IAllocator& source, size_t capacity);

		/**
		 * @brief 領域を取り出し元へ返す。
		 * @details 生きている確保が残っていれば全構成で致命的エラーになる。
		 *          返した後で古いポインタが使われると、原因から離れた場所で壊れるため。
		 */
		~StackAllocator() override;


	public:
		/** @brief ログに出す、人が読む名前。 */
		[[nodiscard]] const char* GetName() const override { return m_name; }

		/**
		 * @brief 先端を進めて確保する。
		 * @param size      利用者が使えるバイト数。
		 * @param alignment 返すアドレスの境界（2 のべき乗であること）。
		 *                  16 未満は 16 に切り上げて扱う。
		 * @return 利用者ポインタ。
		 *         容量を超えるなら nullptr を返し、先端は動かさず、エラーログを 1 行出す。
		 */
		[[nodiscard]] void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override;

		/**
		 * @brief 先端を、この確保を取る前の位置へ戻す。
		 * @param memory Allocate が返したポインタ。
		 *               nullptr を渡してよい（何もしない）。
		 * @details 後に取った物から返すこと。
		 *          先端にある確保でなければ全構成で致命的エラーになる。
		 *          止めずに進めると、先端がまだ使っている確保より手前へ戻り、次の確保がその上に置かれて静かに壊れるため。
		 */
		void Deallocate(void* memory) override;

		/**
		 * @brief 使われ具合を読む。
		 * @details usedBytes は先端の位置で、確保ヘッダと詰め物を含む。
		 */
		[[nodiscard]] AllocatorStatistics GetStatistics() const override;

		/** @brief 今の先端と生きている確保の件数を写す。 */
		[[nodiscard]] StackMarker GetMarker() const;

		/**
		 * @brief 先端と件数を印の値へ戻す。
		 * @param marker このスタックの GetMarker が返した値。
		 * @details 印より後に取った物はまとめて無くなる。
		 *          印の先端が今の先端より後ろならアサートで止まる。
		 *          別のスタックの印かどうかは見分けない。
		 *          印にスタックを持たせる分の太りに見合わないため。
		 */
		void FreeToMarker(StackMarker marker);

		/**
		 * @brief 先端を領域の先頭へ戻す。
		 * @details 先頭で取った印まで戻すのと同じ。
		 */
		void FreeAll();

		/** @brief 生きている確保が 0 件か。 */
		[[nodiscard]] bool IsEmpty() const;


	private:
		/**
		 * @brief 作ったスレッドから呼ばれているかを見る。
		 * @details 公開関数の先頭で呼ぶ。
		 */
		void AssertOwnerThread() const;


	private:
		const char*    m_name;             /**< 人が読む名前。 */
		IAllocator&    m_source;           /**< 取り出し元（壊すときにここへ領域を返す）。 */
		unsigned char* m_region = nullptr; /**< 取り出し元から取った 1 塊の先頭。 */
		size_t         m_capacity;         /**< 領域のバイト数。 */

		size_t   m_top                  = 0; /**< 先端（領域の先頭からのバイト数）。 */
		size_t   m_peakTop              = 0; /**< 先端の最高水位。 */
		uint64_t m_liveAllocationCount  = 0; /**< 生きている確保の件数。 */
		uint64_t m_totalAllocationCount = 0; /**< 作ってからの累計。 */

		std::thread::id m_ownerThread; /**< 作ったスレッド（ここ以外から呼ばれたらアサートする）。 */
	};
} // namespace fang
