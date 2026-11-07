/**
 * @file Log.cpp
 * @brief ログ出力の実装
 */
#include "Log.h"


namespace fangEngine::core
{
	void LogError(const wchar_t* message)
	{
		OutputDebugStringW(message);
		OutputDebugStringW(L"\n");
		MessageBoxW(nullptr, message, L"FangEngine", MB_OK | MB_ICONERROR);
	}


	bool LogIfFailed(HRESULT result, const wchar_t* message)
	{
		if (FAILED(result)) {
			LogError(message);
			return true;
		}
		return false;
	}
} // namespace fangEngine::core
