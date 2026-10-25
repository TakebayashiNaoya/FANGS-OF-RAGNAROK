/**
 * @file Memory.cpp
 * @brief メモリ管理クラスの実装
 */
#include "Memory.h"
#include <cstdlib>
#include <iostream>


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
	// A→B→C順に確保した場合、B の next は A 、previous は C を指す
	Header* next;     // 一つ前に確保したメモリブロックへのポインタ
	Header* previous; // 次に確保したメモリブロックへのポインタ
#endif // _DEBUG
};


void Memory::PrintRemainingBlocks() const
{
#ifdef _DEBUG
	Header* current = m_head;
	while (current != nullptr) {
		const void* block = current;
		const void* data = static_cast<const std::byte*>(block) + HEADER_SIZE;
		std::cout << "メモリアドレス: " << data << " (メモリサイズ: " << current->size << ")" << std::endl;
		current = current->next;
	}
#endif // _DEBUG
}


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

#ifdef _DEBUG
	// 一番最初のメモリブロックの場合、m_head は nullptr なので、if 文でチェックする
	// 今のメモリブロックの previous は、新しく確保したメモリブロックを指す
	if (m_head != nullptr) {
		m_head->previous = header;
	}
	// 今のメモリブロックを新しく確保したメモリブロックの next に設定する
	header->next = m_head;
	// 新しく確保したメモリブロックを m_head に設定する
	m_head = header;
	// 新しく確保したメモリブロックの previous は nullptr にしておく
	m_head->previous = nullptr;
#endif // _DEBUG

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

#ifdef _DEBUG
	Header* header = static_cast<Header*>(block);
	// メモリブロックの前後のリンクを更新する
	if (header->next != nullptr) {
		header->next->previous = header->previous;
	}
	if (header->previous != nullptr) {
		header->previous->next = header->next;
	}
	// 今解放しようとしているメモリブロックの先頭が m_head である場合、m_head を一つ前に更新する
	if (m_head == header) {
		m_head = header->next;
	}
#endif // _DEBUG

	// メモリを解放する
	std::free(block);
}
