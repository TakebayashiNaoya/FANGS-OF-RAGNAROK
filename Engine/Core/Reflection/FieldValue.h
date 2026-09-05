/**
 * @file FieldValue.h
 * @brief フィールドの範囲と、型タグ付きで 1 個の値を運ぶ入れ物。
 */
#pragma once

#include <cstdint>


namespace fang
{
	/** @brief FieldValue / FieldInfo が扱える型。 */
	enum class EnFieldType : uint8_t
	{
		Float,
		Int32,
		Bool,
	};

	/**
	 * @brief 数値フィールドの許容範囲。
	 * @details 既定構築では hasRange が false になり、範囲なし（丸めない）を表す。
	 */
	struct Range
	{
		float minValue = 0.0f;
		float maxValue = 0.0f;
		bool  hasRange = false;

		constexpr Range() = default;

		constexpr Range(float minValueIn, float maxValueIn)
			: minValue(minValueIn)
			, maxValue(maxValueIn)
			, hasRange(true)
		{
		}
	};

	/**
	 * @brief 型タグ付きの値 1 個。
	 * @details TypeInfo::TryGetField / TrySetField が名前だけでどんな型のフィールドにも触れるように、
	 *          このタグ付き共用体 1 本で読み書きを通す。
	 */
	struct FieldValue
	{
		EnFieldType type = EnFieldType::Float;

		union
		{
			float   floatValue = 0.0f;
			int32_t int32Value;
			bool    boolValue;
		};

		[[nodiscard]] static constexpr FieldValue MakeFloat(float value)
		{
			FieldValue result;
			result.type       = EnFieldType::Float;
			result.floatValue = value;
			return result;
		}

		[[nodiscard]] static constexpr FieldValue MakeInt32(int32_t value)
		{
			FieldValue result;
			result.type       = EnFieldType::Int32;
			result.int32Value = value;
			return result;
		}

		[[nodiscard]] static constexpr FieldValue MakeBool(bool value)
		{
			FieldValue result;
			result.type      = EnFieldType::Bool;
			result.boolValue = value;
			return result;
		}
	};
} // namespace fang
