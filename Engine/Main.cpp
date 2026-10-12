#include "Core/Memory/Memory.h"
#include <iostream>
#include <cstdlib>

int main()
{
	Memory memory;
	void* ptr1 = memory.GetMemoryPtr(64);
	void* ptr2 = memory.GetMemoryPtr(32);
	std::cout << "合計メモリサイズ:" << memory.GetTotalMemorySize() << std::endl;
	std::free(ptr1);
	std::free(ptr2);
	return 0;
}
