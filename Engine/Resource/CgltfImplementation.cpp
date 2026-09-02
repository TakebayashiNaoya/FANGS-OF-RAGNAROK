/**
 * @file CgltfImplementation.cpp
 * @brief cgltf の実装をこの翻訳単位 1 本だけに閉じ込める。
 */
#include "Pch.h"

// 上流のコードは /W4 /WX を想定していないので、この TU の中だけ警告を落とす。
// ThirdParty は改変しない方針なので、抑えるのは取り込む側の責任になる。
// C4996 は警告レベルを 0 にしても残ることがあるため個別に落とす（fopen と strcpy を使っている）。
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#pragma warning(pop)
