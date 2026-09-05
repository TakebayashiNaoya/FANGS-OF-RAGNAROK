/**
 * @file TypeInfo.cpp
 * @brief フィールド名での読み書きの実装。
 */
#include "Pch.h"
#include "Core/Reflection/TypeInfo.h"
#include <algorithm>
#include <cstring>


namespace fang
{
	namespace
	{
		/** @brief object の先頭から field.offset だけ進めた位置を、書き込み可能なバイト列として返す。 */
		[[nodiscard]] std::byte* GetFieldAddress(void* object, const FieldInfo& field)
		{
			return static_cast<std::byte*>(object) + field.offset;
		}

		/** @brief 読み取り専用版。 */
		[[nodiscard]] const std::byte* GetFieldAddress(const void* object, const FieldInfo& field)
		{
			return static_cast<const std::byte*>(object) + field.offset;
		}

		/** @brief 範囲を持つフィールドなら、書く前に範囲へ丸める。 */
		[[nodiscard]] float ClampToRange(float value, const Range& range)
		{
			if (!range.hasRange)
			{
				return value;
			}

			return std::clamp(value, range.minValue, range.maxValue);
		}
	} // namespace


	const FieldInfo* TypeInfo::FindField(std::string_view fieldName) const
	{
		for (const FieldInfo& field : fields)
		{
			if (fieldName == field.name)
			{
				return &field;
			}
		}

		return nullptr;
	}


	bool TypeInfo::TryGetField(const void* object, std::string_view fieldName, FieldValue* outValue) const
	{
		const FieldInfo* field = FindField(fieldName);
		if (field == nullptr)
		{
			return false;
		}

		const std::byte* address = GetFieldAddress(object, *field);

		switch (field->type)
		{
			case EnFieldType::Float:
				*outValue = FieldValue::MakeFloat(*reinterpret_cast<const float*>(address));
				return true;

			case EnFieldType::Int32:
				*outValue = FieldValue::MakeInt32(*reinterpret_cast<const int32_t*>(address));
				return true;

			case EnFieldType::Bool:
				*outValue = FieldValue::MakeBool(*reinterpret_cast<const bool*>(address));
				return true;
		}

		return false;
	}


	bool TypeInfo::TrySetField(void* object, std::string_view fieldName, const FieldValue& value) const
	{
		const FieldInfo* field = FindField(fieldName);
		if (field == nullptr)
		{
			return false;
		}

		if (field->type != value.type)
		{
			return false;
		}

		std::byte* address = GetFieldAddress(object, *field);

		switch (field->type)
		{
			case EnFieldType::Float:
				*reinterpret_cast<float*>(address) = ClampToRange(value.floatValue, field->range);
				return true;

			case EnFieldType::Int32: *reinterpret_cast<int32_t*>(address) = value.int32Value; return true;

			case EnFieldType::Bool: *reinterpret_cast<bool*>(address) = value.boolValue; return true;
		}

		return false;
	}
} // namespace fang
