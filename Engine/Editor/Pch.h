/**
 * @file Pch.h
 * @brief Editor のプリコンパイル済みヘッダ。
 * @details 標準ライブラリと Core の基本型だけを入れる。
 *          モジュール固有のヘッダはここに足さない。触ると全 TU が再コンパイルされる。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Log/Assert.h"
#include "Core/Log/Log.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
