/**
 * @file Input.cpp
 * @brief Input モジュールの識別とログカテゴリの定義。
 */
#include "Pch.h"
#include "Input/Input.h"
#include "Input/InputLog.h"


FANG_DEFINE_LOG_CATEGORY(Input);


namespace fang
{
	const char* GetInputModuleName()
	{
		return "Input";
	}
} // namespace fang
