/**
 * @file Main.cpp
 * @brief メイン関数の実装
 */
#include <cstdlib>
#include <iostream>
#include "Application.h"
#include "Core/Memory/Memory.h"


int main()
{
	Application application;
	if (application.Initialize() == false) return EXIT_FAILURE;
	if (application.Run() == false)        return EXIT_FAILURE;

	core::Memory memory;
	void* ptr1 = memory.MallocMemory(64);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	void* ptr2 = memory.MallocMemory(32);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	memory.FreeMemory(ptr1);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	memory.FreeMemory(ptr2);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.PrintRemainingBlocks();
	return 0;
}
