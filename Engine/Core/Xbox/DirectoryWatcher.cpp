/**
 * @file DirectoryWatcher.cpp
 * @brief ディレクトリの変化の見張りの実装（Xbox / UWP）。
 */
#include "Pch.h"
#include "Core/Platform/DirectoryWatcher.h"


namespace fang
{
	DirectoryWatcher::~DirectoryWatcher()
	{
		Shutdown();
	}


	bool DirectoryWatcher::Initialize(const char*)
	{
		// UWP はパッケージの外を見張れず、中のファイルは実行中に書き換わらない。
		// 呼ぶ側は「見張れなかった」として扱えばよいので、ここでログは出さない。
		return false;
	}


	void DirectoryWatcher::Shutdown()
	{
		m_nativeHandle = nullptr;
	}


	bool DirectoryWatcher::ConsumeChange()
	{
		return false;
	}
} // namespace fang
