/**
 * @file Memory.h
 * @brief メモリ管理クラス
 */
#pragma once
#include <cstddef>


class Memory
{
public:
	/**
	 * @brief メモリを確保する
	 * @param size 確保するメモリのサイズ
	 * @return 確保したメモリのポインタ
	 */
	void* MallocMemory(size_t size);

	/**
	 * @brief メモリを解放する
	 * @param ptr 解放するメモリのポインタ
	 */
	void FreeMemory(void* ptr);

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
	size_t m_size  = 0; // 合計メモリサイズ
	size_t m_count = 0; // 確保したメモリの数
};
