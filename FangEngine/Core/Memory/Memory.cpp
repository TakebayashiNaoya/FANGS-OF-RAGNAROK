/**
 * @file Memory.cpp
 * @brief メモリ管理クラスの実装
 */
#include "Memory.h"
#include <cstdlib>
#include <cstdint>


namespace fangEngine::core
{
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
		// A→B→C順に確保した場合、BのnextはA、previousはCを指す
		Header* next;     // 一つ前に確保したメモリブロックへのポインタ
		Header* previous; // 次に確保したメモリブロックへのポインタ
#endif // _DEBUG
	};


	std::vector<Memory::BlockInformation> Memory::GetRemainingBlocks() const
	{
		std::vector<BlockInformation> blocks;

#ifdef _DEBUG
		Header* current = m_head;
		while (current != nullptr) {
			const void* block = current;
			const void* data = static_cast<const std::byte*>(block) + HEADER_SIZE;
			blocks.push_back({ data, current->size });
			current = current->next;
		}
#endif // _DEBUG

		return blocks;
	}


	void* Memory::MallocMemory(size_t size)
	{
		// ヘッダーを含めた合計サイズがsize_t型の最大値を超える場合nullptrを返す
		if (size > SIZE_MAX - HEADER_SIZE) return nullptr;

		// メモリを確保する際に、ヘッダーサイズ分の余裕を持たせる
		void* block = std::malloc(size + HEADER_SIZE);

		if (block == nullptr) return nullptr;

		// ヘッダーにサイズ情報を格納する
		Header* header = static_cast<Header*>(block);
		header->size = size;
		// ヘッダーの後ろのメモリ領域を返す
		void* data = static_cast<std::byte*>(block) + HEADER_SIZE;
		// 合計メモリサイズを更新する
		m_size += size;
		// 確保したメモリの数を更新する
		m_count++;

#ifdef _DEBUG
		// 一番最初のメモリブロックの場合、m_headはnullptrなので、if文でチェックする
		// 今のメモリブロックのpreviousは、新しく確保したメモリブロックを指す
		if (m_head != nullptr) {
			m_head->previous = header;
		}
		// 今のメモリブロックを新しく確保したメモリブロックのnextに設定する
		header->next = m_head;
		// 新しく確保したメモリブロックをm_headに設定する
		m_head = header;
		// 新しく確保したメモリブロックのpreviousはnullptrにしておく
		m_head->previous = nullptr;
#endif // _DEBUG

		return data;
	}


	void Memory::FreeMemory(void* data)
	{
		if (data == nullptr)	return;

		// ヘッダーの前のメモリ領域を取得する
		void* block = static_cast<std::byte*>(data) - HEADER_SIZE;
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
		// 今解放しようとしているメモリブロックの先頭がm_headである場合、m_headを一つ前に更新する
		if (m_head == header) {
			m_head = header->next;
		}
#endif // _DEBUG

		// メモリを解放する
		std::free(block);
	}
} // namespace fangEngine::core
