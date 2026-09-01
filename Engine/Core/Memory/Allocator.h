/**
 * @file Allocator.h
 * @brief アロケータのインターフェースと生成・破棄ヘルパ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <new>
#include <utility>


namespace fang
{
	/**
	 * @brief アロケータのインターフェース。
	 * @details 生 new / delete はこの実装の中だけで使う。
	 * @threading 実装ごとに違う。派生クラスの @threading を見ること。
	 */
	class IAllocator
	{
	public:
		static constexpr size_t DEFAULT_ALIGNMENT = 16;

		virtual ~IAllocator() = default;

		/** @brief 人が読む名前。ログとプロファイラ用。 */
		[[nodiscard]] virtual const char* GetName() const = 0;


	public:
		/**
		 * @brief 確保する。
		 * @param size 確保するバイト数。
		 * @param alignment 先頭アドレスの境界（バイト）。2 のべき乗であること。
		 * @return 確保した領域。失敗したら nullptr。
		 */
		[[nodiscard]] virtual void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) = 0;

		/**
		 * @brief 解放する。
		 * @param memory Allocate が返したポインタ。nullptr を渡してよい（何もしない）。
		 */
		virtual void Deallocate(void* memory) = 0;
	};

	/**
	 * @brief CRT のヒープをそのまま使うアロケータ。
	 * @details ロード時とエンジンの初期化にだけ使う。毎フレームの確保には使わない。
	 * @threading 任意のスレッド（CRT が同期する）。
	 */
	class HeapAllocator final : public IAllocator
	{
	public:
		/** @brief 人が読む名前。常に "Heap"。 */
		[[nodiscard]] const char* GetName() const override { return "Heap"; }

		/** @brief エンジン全体で使う既定のヒープ。 */
		[[nodiscard]] static HeapAllocator& GetInstance();


	public:
		[[nodiscard]] void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override;
		void                Deallocate(void* memory) override;
	};

	/** @brief アロケータ上にオブジェクトを作る。確保に失敗したら nullptr（コンストラクタは呼ばれない）。 */
	template <typename T, typename... Args> [[nodiscard]] inline T* New(IAllocator& allocator, Args&&... args)
	{
		void* memory = allocator.Allocate(sizeof(T), alignof(T));
		if (memory == nullptr)
		{
			return nullptr;
		}

		return ::new (memory) T(std::forward<Args>(args)...);
	}

	/**
	 * @brief New で作ったオブジェクトを壊して返す。
	 * @param allocator New のときと同じアロケータであること。
	 * @param object nullptr を渡してよい（何もしない）。
	 */
	template <typename T> inline void Delete(IAllocator& allocator, T* object)
	{
		if (object == nullptr)
		{
			return;
		}

		object->~T();
		allocator.Deallocate(object);
	}
} // namespace fang
