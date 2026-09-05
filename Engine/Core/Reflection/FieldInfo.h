/**
 * @file FieldInfo.h
 * @brief フィールド 1 個ぶんの素性（名前・表示名・型・オフセット・範囲）。
 */
#pragma once

#include "Core/Reflection/FieldValue.h"
#include <cstddef>
#include <cstdint>


namespace fang
{
	/** @brief C++ の型から EnFieldType を引く。特殊化していない型を渡すとリンクエラーになる。 */
	template <typename T> [[nodiscard]] constexpr EnFieldType GetFieldType();

	template <> [[nodiscard]] constexpr EnFieldType GetFieldType<float>()
	{
		return EnFieldType::Float;
	}

	template <> [[nodiscard]] constexpr EnFieldType GetFieldType<int32_t>()
	{
		return EnFieldType::Int32;
	}

	template <> [[nodiscard]] constexpr EnFieldType GetFieldType<bool>()
	{
		return EnFieldType::Bool;
	}

	/**
	 * @brief フィールド 1 個ぶんの素性。
	 * @details TypeInfo が持つ constexpr の配列の要素。offset は所有者の先頭からのバイト位置で、
	 *          FANG_FIELD マクロが offsetof から埋める。
	 */
	struct FieldInfo
	{
		const char* name        = nullptr; /**< コード上の識別子と同じ綴り。 */
		const char* displayName = nullptr; /**< インスペクタに出す表示名。日本語。 */
		EnFieldType type        = EnFieldType::Float;
		size_t      offset      = 0;
		Range       range;
	};

	/**
	 * @brief FieldInfo を 1 個組み立てる。
	 * @tparam T フィールドの宣言型。FANG_FIELD が decltype から渡す。
	 */
	template <typename T>
	[[nodiscard]] constexpr FieldInfo MakeFieldInfo(
		const char* name,
		const char* displayName,
		size_t      offset,
		Range       range = {}
	)
	{
		return FieldInfo{ name, displayName, GetFieldType<T>(), offset, range };
	}
} // namespace fang
