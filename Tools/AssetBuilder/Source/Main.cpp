/**
 * @file Main.cpp
 * @brief アセット変換ツールのエントリポイント。
 */
#include "Core/Core.h"
#include "Resource/Resource.h"
#include <cstdio>


// TODO: FBX からバイナリへの変換とテクスチャ圧縮。
int main()
{
	std::printf("AssetBuilder (%s / %s)\n", fang::GetCoreModuleName(), fang::GetResourceModuleName());
	return 0;
}
