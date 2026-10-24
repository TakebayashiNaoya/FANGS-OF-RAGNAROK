#include "Core/Memory/Memory.h"
#include <iostream>


int main()
{
	Memory memory;
	void* ptr1 = memory.MallocMemory(64);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	void* ptr2 = memory.MallocMemory(32);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.FreeMemory(ptr1);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	memory.FreeMemory(ptr2);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::cout << "確保したメモリの数:" << memory.GetMemoryCount() << std::endl;
	return 0;
}
