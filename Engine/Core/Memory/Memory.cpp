#include "Memory.h"
#include <cstdlib>


void* Memory::GetMemoryPtr(int size)
{
	m_size += size;
	void* ptr = std::malloc(size);
	return ptr;
}
