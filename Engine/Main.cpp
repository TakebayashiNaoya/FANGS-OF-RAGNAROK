#include "Core/Memory/Memory.h"
#include <iostream>


int main()
{
	Memory memory;
	void* ptr1 = memory.MallocMemoryPtr(64);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	void* ptr2 = memory.MallocMemoryPtr(32);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	memory.FreeMemoryPtr(ptr1);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	memory.FreeMemoryPtr(ptr2);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	return 0;
}
