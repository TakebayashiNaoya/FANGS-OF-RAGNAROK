/**
 * @file FrameAllocator.h
 * @brief フレーム境界でまとめて空けるリニアアロケータと、その 2 枚組。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Memory/Allocator.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>


namespace fang
{
	/** @brief フレームメモリの生成条件。 */
	struct FrameMemoryDesc
	{
		size_t capacityPerBuffer = 16 * 1024 * 1024; /**< 1 枚あたりのバイト数。起動時にこの 2 枚分を確保する。 */
	};

	/**
	 * @brief 確保はオフセットを進めるだけ、解放はフレーム境界で一括にするアロケータ。
	 * @details 個別の Deallocate は何もせず、デストラクタも呼ばれない。➡置いてよいのは自明に壊せる型だけで、
	 *          オブジェクトを作るときは NewFrame を通せばその制約をコンパイル時に確かめられる。
	 * @threading Allocate は任意のスレッドから同時に呼べる。Initialize / Shutdown / Reset と値の取得は
	 *            メインスレッドのみ。
	 */
	class FrameAllocator final : public IAllocator
	{
	public:
		FANG_NON_COPYABLE(FrameAllocator);
		FANG_NON_MOVABLE(FrameAllocator);

		FrameAllocator() = default;
		~FrameAllocator() override;

		/** @brief 人が読む名前。Initialize で受け取ったもの。 */
		[[nodiscard]] const char* GetName() const override { return m_name; }

		/** @brief 土台のバイト数。実行中は変わらない。 */
		[[nodiscard]] FANG_FORCEINLINE size_t GetCapacity() const { return m_capacity; }

		/** @brief 直前のリセットから進んだバイト数。満杯を踏んだ後は容量を超えた「要求された量」になる。 */
		[[nodiscard]] FANG_FORCEINLINE size_t GetUsedBytes() const { return m_offset.load(std::memory_order_relaxed); }


	public:
		/**
		 * @brief 土台のメモリを一括で確保する。
		 * @param sourceAllocator Shutdown まで生きていること。土台を返すときにも同じものを使う。
		 * @param capacity        バイト数。0 は受け付けない。
		 * @param name            ログに出す名前。文字列の寿命は呼ぶ側が持つ。
		 * @return 土台を確保できなければ false。
		 */
		[[nodiscard]] bool Initialize(IAllocator& sourceAllocator, size_t capacity, const char* name);

		/** @brief 土台を元のアロケータへ返す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief 確保位置を先頭へ戻す。ここまでに配ったポインタは全部無効になる。
		 * @details 配った領域に触っているジョブが残っていないことは、呼ぶ側が保証すること。
		 */
		void Reset();

		/**
		 * @brief 確保する。オフセットを進めるだけなので、失敗しても中身は壊れない。
		 * @param size      バイト数。内部で 16 の倍数へ切り上げる。
		 * @param alignment 2 のべき乗であること。16 を超える分は余分に取って端を捨てる。
		 * @return 確保した領域。容量が足りなければ nullptr（エラーログを出し、Debug では止まる）。
		 */
		[[nodiscard]] void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override;

		/** @brief 何もしない。この置き場はフレーム境界の Reset でまとめて空く。 */
		void Deallocate(void* memory) override;

#if FANG_ENABLE_PROFILER
		/** @brief 起動以降のどのフレームでも超えなかった使用量。容量を詰めるときの目安。 */
		[[nodiscard]] FANG_FORCEINLINE size_t GetPeakUsedBytes() const { return m_peakUsedBytes; }

		/** @brief 直前のリセットからの Allocate 回数。細切れに取りすぎていないかを見る。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetAllocationCount() const
		{
			return m_allocationCount.load(std::memory_order_relaxed);
		}
#endif


	private:
		IAllocator*    m_sourceAllocator = nullptr;
		unsigned char* m_memory          = nullptr; /**< 土台の先頭。16 境界に揃っている。 */

		size_t      m_capacity = 0;
		const char* m_name     = "Frame"; /**< Initialize までの仮の名前。 */

		/** @brief 次に配る位置。Allocate はこれを fetch_add するだけなので、ロックもリストも要らない。 */
		std::atomic<size_t> m_offset{ 0 };

#if FANG_ENABLE_ASSERT
		/**
		 * @brief Reset が偶数 ➡ 奇数 ➡ 偶数と進めるカウンタ。
		 * @details 奇数の間に走った Allocate は、配った先を同じ Reset に踏み倒される。➡Allocate 側は偶奇を
		 *          見るだけで、リセットとの競合をその場で捕まえられる。
		 */
		std::atomic<uint32_t> m_resetGeneration{ 0 };
#endif

#if FANG_ENABLE_PROFILER
		std::atomic<uint32_t> m_allocationCount{ 0 };
		size_t                m_peakUsedBytes = 0; /**< Reset のたびに更新する、起動以降の最大使用量。 */
#endif
	};

	/***************************************/

	/**
	 * @brief FrameAllocator を 2 枚持ち、フレーム境界で入れ替える置き場。
	 * @details 入れ替えた側だけをリセットするので、前のフレームに確保したデータは次のフレームの間ずっと読める。
	 *          ➡ゲーム更新と描画を 1 フレームずらして並走させても、読む先が消えない。
	 * @threading Initialize / Shutdown / BeginFrame はメインスレッドのみ。取り出した FrameAllocator への
	 *            Allocate は任意のスレッドから呼べる。
	 */
	class FrameMemory
	{
	public:
		FANG_NON_COPYABLE(FrameMemory);
		FANG_NON_MOVABLE(FrameMemory);

		static constexpr size_t BUFFER_COUNT = 2; /**< 今のフレームと 1 つ前のフレームで 2 枚。 */

		FrameMemory() = default;

		/** @brief 1 枚あたりのバイト数。Initialize の前は 0。 */
		[[nodiscard]] FANG_FORCEINLINE size_t GetCapacityPerBuffer() const { return m_buffers[0].GetCapacity(); }

		/** @brief 今のフレームの置き場。 */
		[[nodiscard]] FANG_FORCEINLINE FrameAllocator& GetCurrent() { return m_buffers[m_currentIndex]; }

		/** @brief 前のフレームの置き場。読み取りに使い、ここへ新しく確保しない。 */
		[[nodiscard]] FANG_FORCEINLINE FrameAllocator& GetPrevious()
		{
			return m_buffers[(m_currentIndex + 1) % BUFFER_COUNT];
		}


	public:
		/**
		 * @brief 2 枚分の土台をヒープから確保する。
		 * @return どちらか 1 枚でも確保できなければ false。そのときは 2 枚とも解放済み。
		 */
		[[nodiscard]] bool Initialize(const FrameMemoryDesc& desc);

		/** @brief 2 枚とも解放する。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief 2 枚を入れ替え、新しく今のフレームになった側だけをリセットする。
		 * @details 前のフレームの 1 枚は触らないので、そこへ確保したデータはこのフレームの間も生きている。
		 */
		void BeginFrame();


	private:
		FrameAllocator m_buffers[BUFFER_COUNT];
		size_t         m_currentIndex = 0; /**< 今のフレームが使っている側。BeginFrame が 1 つ進める。 */
	};

	/**
	 * @brief フレーム寿命のオブジェクトを 1 つ作る。
	 * @param allocator 置き場。確保できなければ何も構築しない。
	 * @return 作ったオブジェクト。確保に失敗したら nullptr。
	 * @details デストラクタが呼ばれないまま消えるので、自明に壊せる型しか通さない。
	 */
	template <typename T, typename... Args> [[nodiscard]] inline T* NewFrame(FrameAllocator& allocator, Args&&... args)
	{
		static_assert(
			std::is_trivially_destructible_v<T>,
			"フレームアロケータはデストラクタを呼ばずに捨てるので、自明に壊せる型しか置けない"
		);

		void* memory = allocator.Allocate(sizeof(T), alignof(T));
		if (memory == nullptr)
		{
			return nullptr;
		}

		return ::new (memory) T(std::forward<Args>(args)...);
	}
} // namespace fang
