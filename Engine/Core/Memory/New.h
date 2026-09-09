/**
 * @file New.h
 * @brief アロケータを名指しする new と、対になる delete。
 * @details 確保は new (allocator) T(...)、破棄は delete p と書く。
 *          取り出し元は利用者ポインタの直前のヘッダに入っているので、delete も std::unique_ptr もそのまま使える。
 *          境界は書かない。alignof(T) が既定より大きい型は std::align_val_t 付きの形が自動で選ばれる。
 */
#pragma once

#include "Core/Memory/Allocator.h"
#include <memory>
#include <new>
#include <source_location>
#include <utility>


/** @brief アロケータから 1 個ぶん確保する。確保できなければ致命的エラーで止まる。 */
[[nodiscard]] void* operator new(
	size_t               size,
	fang::IAllocator&    allocator,
	std::source_location location = std::source_location::current()
);

/** @brief 既定より大きい境界を要る型を、アロケータから 1 個ぶん確保する。 */
[[nodiscard]] void* operator new(
	size_t               size,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location = std::source_location::current()
);

/** @brief アロケータから配列ぶん確保する。 */
[[nodiscard]] void* operator new[](
	size_t               size,
	fang::IAllocator&    allocator,
	std::source_location location = std::source_location::current()
);

/** @brief 既定より大きい境界を要る型の配列を、アロケータから確保する。 */
[[nodiscard]] void* operator new[](
	size_t               size,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location = std::source_location::current()
);

/**
 * @brief 上の new と対になる delete。
 * @details 例外を切っているので実際には呼ばれない。宣言しないとコンパイラが C4291 を出す。
 */
void operator delete(void* memory, fang::IAllocator& allocator, std::source_location location) noexcept;
void operator delete(
	void*                memory,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location
) noexcept;
void operator delete[](void* memory, fang::IAllocator& allocator, std::source_location location) noexcept;
void operator delete[](
	void*                memory,
	std::align_val_t     alignment,
	fang::IAllocator&    allocator,
	std::source_location location
) noexcept;


namespace fang
{
	/**
	 * @brief アロケータから確保して、所有を std::unique_ptr で持つ。
	 * @param allocator 取り出し元。
	 * @param args      コンストラクタへ渡す引数。
	 * @details 既定のデリータの delete がヘッダを見て同じアロケータへ返すので、デリータを差し替えなくてよい。
	 */
	template <typename T, typename... Args>
	[[nodiscard]] std::unique_ptr<T> MakeUnique(IAllocator& allocator, Args&&... args)
	{
		return std::unique_ptr<T>(new (allocator) T(std::forward<Args>(args)...));
	}
} // namespace fang
