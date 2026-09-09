/**
 * @file HeapAllocator.h
 * @brief 名前を持つ汎用ヒープ。
 */
#pragma once

#include "Core/Memory/Allocator.h"
#include <atomic>


namespace fang
{
	class HeapAllocator;


	namespace detail
	{
		union DefaultHeapStorage;
	} // namespace detail


	/**
	 * @brief 名前を付けたヒープを作る。実体は既定のヒープから取る。
	 * @param name ログと報告に出す名前。寿命を持たないので文字列リテラルを渡すこと。
	 * @details 作ったヒープは登録簿につながり、ForEachHeap で回れるようになる。
	 *          使い終わったら DestroyHeap で壊すこと。壊し忘れは ReportAllHeapLeaks が見つける。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] HeapAllocator& CreateHeap(const char* name);

	/**
	 * @brief 名前を付けたヒープを壊す。
	 * @details 壊す前に生きている確保が無いかを見る。残っていればリークとして報告し、アサートで止める。
	 *          既定のヒープは壊せない（渡したらアサート）。
	 * @threading 任意のスレッド。ただし、そのヒープを使っている確保が他のスレッドで走っていないこと。
	 */
	void DestroyHeap(HeapAllocator& heap);

	/**
	 * @brief 今ある名前付きヒープを順に渡す。既定のヒープは含まない。
	 * @param visitor  各ヒープに対して呼ぶ関数。ここから CreateHeap / DestroyHeap を呼ばないこと。
	 * @param userData visitor へそのまま渡す。
	 * @details 回っている間は登録簿を止めるので、短く済ませること。
	 *          既定のヒープを含めないのは、定数初期化の順番に登録簿を依存させないため。
	 *          既定のヒープも見たい側は HeapAllocator::GetInstance() を自分で足す。
	 * @threading 任意のスレッド。
	 */
	void ForEachHeap(void (*visitor)(HeapAllocator& heap, void* userData), void* userData);

	/**
	 * @brief 既定のヒープと、壊し忘れた名前付きヒープのリークを報告する。
	 * @return リークの件数。
	 * @details 終了処理の一番最後から呼ぶ。既定のヒープは静的な物の破棄と混ざるので、止めずに警告だけ出す。
	 * @threading 任意のスレッド。
	 */
	uint64_t ReportAllHeapLeaks();


	/**
	 * @brief 名前を持つ汎用ヒープ。
	 * @details ロード時とエンジンの初期化に使う。ゲームが動いている間のフレームの中では確保しない。
	 *          サブシステムは自分のヒープを持ってよく、名前がそのままメモリの内訳になる。
	 *          確保のたびに利用者ポインタの直前へ確保ヘッダを書くので、delete で返せる。
	 * @threading 任意のスレッド。裏の CRT が同期し、統計は atomic で数える。
	 */
	class HeapAllocator final : public IAllocator
	{
	public:
		FANG_NON_COPYABLE(HeapAllocator);
		FANG_NON_MOVABLE(HeapAllocator);


	public:
		/** @brief エンジン全体で使う既定のヒープ。アロケータを名指ししない new はここへ落ちる。 */
		[[nodiscard]] static HeapAllocator& GetInstance();


	public:
		[[nodiscard]] const char* GetName() const override { return m_name; }

		[[nodiscard]] void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override;
		void                Deallocate(void* memory) override;

		[[nodiscard]] AllocatorStatistics GetStatistics() const override;

		/**
		 * @brief まだ返っていない確保を報告する。
		 * @return リークの件数。
		 * @details 追跡を入れていない構成では、生きている件数だけを報告して呼び出し元は分からない。
		 * @threading 任意のスレッド。回っている間はそのヒープの確保を止める。
		 */
		[[nodiscard]] uint64_t ReportLeaks() const;


	private:
		/**
		 * @brief 名前を付けて作る。
		 * @details 作れるのは CreateHeap と既定のヒープの置き場だけ。
		 *          スタックに置くと、そこから取ったメモリの方が長生きしたときに返す先が消える。
		 */
		constexpr explicit HeapAllocator(const char* name)
			: m_name(name)
		{
		}


	private:
		/** @brief 使用量の最高水位を上げる。他のスレッドがもっと高い水位を書いていたらそちらを残す。 */
		void UpdatePeakBytes(uint64_t usedBytes);

#if FANG_ENABLE_MEMORY_TRACKING
		/** @brief 追跡記録を生存リストの先頭へつなぐ。 */
		void LinkAllocationRecord(AllocationRecord* record);

		/** @brief 追跡記録を生存リストから外す。 */
		void UnlinkAllocationRecord(AllocationRecord* record);
#endif


	private:
		const char*    m_name;               /**< 人が読む名前。 */
		HeapAllocator* m_nextHeap = nullptr; /**< 登録簿の次。既定のヒープは登録簿に入らないので常に nullptr。 */

		std::atomic<uint64_t> m_usedBytes            = 0; /**< 今生きている量。 */
		std::atomic<uint64_t> m_peakBytes            = 0; /**< 使用量の最高水位。 */
		std::atomic<uint64_t> m_liveAllocationCount  = 0; /**< 今生きている件数。 */
		std::atomic<uint64_t> m_totalAllocationCount = 0; /**< 起動からの累計。 */

#if FANG_ENABLE_MEMORY_TRACKING
		AllocationRecord* m_liveListHead = nullptr; /**< まだ返っていない確保の双方向リスト。 */

		/** @brief 生存リストを触る間の錠。ReportLeaks は const なので mutable にしてある。 */
		mutable std::atomic_flag m_liveListLock{};
#endif

		friend HeapAllocator& CreateHeap(const char* name);
		friend void           DestroyHeap(HeapAllocator& heap);
		friend void           ForEachHeap(void (*visitor)(HeapAllocator& heap, void* userData), void* userData);
		friend union detail::DefaultHeapStorage;
	};


	namespace detail
	{
		/**
		 * @brief 既定のヒープの置き場。
		 * @details 共用体のメンバは明示的に壊さない限り破棄されない。
		 *          プロセスの終了処理から来る解放を最後まで受け取れるように、この形で持つ。
		 *          既定のヒープだけは自分自身から取れないので、静的記憶域に置く唯一の例外になる。
		 */
		union DefaultHeapStorage
		{
			HeapAllocator heap;

			constexpr DefaultHeapStorage()
				: heap("Default")
			{
			}
			~DefaultHeapStorage() {}
		};
	} // namespace detail
} // namespace fang
