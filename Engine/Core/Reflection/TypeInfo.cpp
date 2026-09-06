/**
 * @file TypeInfo.cpp
 * @brief フィールド名・アドレスでの読み書きの実装。
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

		return ReadFieldValue(GetFieldAddress(object, *field), field->type, outValue);
	}


	bool TypeInfo::TrySetField(void* object, std::string_view fieldName, const FieldValue& value) const
	{
		const FieldInfo* field = FindField(fieldName);
		if (field == nullptr)
		{
			return false;
		}

		return WriteFieldValue(GetFieldAddress(object, *field), field->type, field->range, value);
	}


	bool ReadFieldValue(const void* address, EnFieldType type, FieldValue* outValue)
	{
		switch (type)
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

			case EnFieldType::Struct: return false;
		}

		return false;
	}


	bool WriteFieldValue(void* address, EnFieldType type, const Range& range, const FieldValue& value)
	{
		if (type != value.type)
		{
			return false;
		}

		switch (type)
		{
			case EnFieldType::Float:
				*reinterpret_cast<float*>(address) = ClampToRange(value.floatValue, range);
				return true;

			case EnFieldType::Int32: *reinterpret_cast<int32_t*>(address) = value.int32Value; return true;

			case EnFieldType::Bool: *reinterpret_cast<bool*>(address) = value.boolValue; return true;

			case EnFieldType::Struct: return false;
		}

		return false;
	}
} // namespace fang
