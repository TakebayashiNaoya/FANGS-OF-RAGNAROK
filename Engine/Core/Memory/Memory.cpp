/**
 * @file Memory.cpp
 * @brief メモリ管理クラスの実装
 */
#include "Memory.h"
#include <cstdlib>


namespace
{
#ifdef _DEBUG
	constexpr size_t HEADER_SIZE = 32;
#else
	constexpr size_t HEADER_SIZE = 16;
#endif // _DEBUG
}


struct Memory::Header
{
	size_t size; // メモリブロックのサイズ

#ifdef _DEBUG
	// A→B→C順に確保した場合、B の next は A 、previous は C を指す。
	Header* next;     // 一つ前に確保したメモリブロックへのポインタ
	Header* previous; // 次に確保したメモリブロックへのポインタ
#endif // _DEBUG
};


void* Memory::MallocMemory(size_t size)
{
	// メモリを確保する際に、ヘッダーサイズ分の余裕を持たせる
	void* ptr = std::malloc(size + HEADER_SIZE);

	if (ptr == nullptr)	return nullptr;

	// ヘッダーにサイズ情報を格納する
	Header* header = static_cast<Header*>(ptr);
	header->size = size;
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
	size_t size = static_cast<Header*>(block)->size;
	// 合計メモリサイズを更新する
	m_size -= size;
	// 確保したメモリの数を更新する
	m_count--;
	// メモリを解放する
	std::free(block);
}
