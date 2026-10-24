/**
 * @file Memory.cpp
 * @brief メモリ管理クラスの実装
 */
#include "Memory.h"
#include <cstdlib>


namespace
{
	constexpr size_t HEADER_SIZE = 16;
}


void* Memory::MallocMemory(size_t size)
{
	// メモリを確保する際に、ヘッダーサイズ分の余裕を持たせる
	void* ptr = std::malloc(size + HEADER_SIZE);

	if (ptr == nullptr)	return nullptr;

	// ヘッダーにサイズ情報を格納する
	size_t* header = static_cast<size_t*>(ptr);
	*header = size;
	// ヘッダーの後ろのメモリ領域を返す
	ptr = static_cast<std::byte*>(ptr) + HEADER_SIZE;
	// 合計メモリサイズを更新する
	m_size += size;
	// 確保したメモリの数を更新する
	m_count++;

	return ptr;
}


void Memory::FreeMemory(void* ptr)
{
	if (ptr == nullptr)	return;

	// ヘッダーの前のメモリ領域を取得する
	void* block = static_cast<std::byte*>(ptr) - HEADER_SIZE;
	// ヘッダーに格納されたサイズ情報を取得する
	size_t size = *static_cast<size_t*>(block);
	// 合計メモリサイズを更新する
	m_size -= size;
	// 確保したメモリの数を更新する
	m_count--;
	// メモリを解放する
	std::free(block);
}
