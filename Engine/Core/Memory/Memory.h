/**
 * @file Memory.h
 * @brief メモリ管理クラス
 */
#pragma once
#include <cstddef>


/**
 * @brief メモリ管理クラス
 * @details 解放し忘れが見えなくなるため、破棄しても残っているメモリブロックは解放しない。
 */
class Memory
{
public:
	/**
	 * @brief メモリ上に残っているメモリブロックの情報を出力する
	 */
	void PrintRemainingBlocks() const;

	/**
	 * @brief メモリを確保する
	 * @param size 確保するメモリのサイズ
	 * @return 確保したメモリのポインタ
	 */
	void* MallocMemory(size_t size);

	/**
	 * @brief メモリを解放する
	 * @param data 解放するメモリのポインタ
	 */
	void FreeMemory(void* data);

	/**
	 * @brief 合計メモリサイズを取得する
	 * @return 合計メモリサイズ
	 */
	inline size_t GetTotalMemorySize() const { return m_size; }

	/**
	 * @brief 確保したメモリの数を取得する
	 * @return 確保したメモリの数
	 */
	inline size_t GetMemoryCount() const { return m_count; }


private:
	struct Header;
#ifdef _DEBUG
	Header* m_head = nullptr; // 最後に確保したメモリブロックの先頭
#endif // _DEBUG


private:
	size_t m_size  = 0; // 合計メモリサイズ
	size_t m_count = 0; // 確保したメモリの数
};
