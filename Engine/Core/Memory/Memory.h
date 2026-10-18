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
	void* MallocMemoryPtr(size_t size);

	/**
	 * @brief メモリを解放する
	 * @param ptr 解放するメモリのポインタ
	 */
	void FreeMemoryPtr(void* ptr);

	/**
	 * @brief 合計メモリサイズを取得する
	 * @return 合計メモリサイズ
	 */
	inline const size_t GetTotalMemorySize() const { return m_size; }


private:
	size_t m_size = 0;
};
