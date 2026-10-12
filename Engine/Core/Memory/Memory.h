#pragma once


class Memory
{
public:
	void* GetMemoryPtr(int size);

	constexpr int GetTotalMemorySize() const { return m_size; }

private:
	int m_size = 0;
};
