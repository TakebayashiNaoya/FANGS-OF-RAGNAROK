/**
 * @file ReflectionTests.cpp
 * @brief フィールド名での読み書き、型違い、範囲の丸め、未知の名前を確かめる。
 */
#include "Core/Reflection/Reflection.h"
#include <doctest.h>


namespace
{
	/** @brief float / int32_t / bool の 3 種を持つ、テスト専用の反射対象。 */
	struct ReflectionTestParameter
	{
		FANG_REFLECT_BEGIN(ReflectionTestParameter)
		FANG_FIELD(speed, "速度", fang::Range(0.0f, 10.0f))
		FANG_FIELD(count, "個数")
		FANG_FIELD(isActive, "有効")
		FANG_REFLECT_END()

		float   speed    = 1.0f;
		int32_t count    = 0;
		bool    isActive = false;
	};

	/** @brief 入れ子を 1 段持つ、テスト専用の反射対象。名前での読み書きが Struct を弾くことを確かめる。 */
	struct ReflectionTestNestedParameter
	{
		FANG_REFLECT_BEGIN(ReflectionTestNestedParameter)
		FANG_FIELD_NESTED(inner, "内側")
		FANG_REFLECT_END()

		ReflectionTestParameter inner;
	};
} // namespace


TEST_CASE("Reflection: フィールド名で読み書きできる")
{
	ReflectionTestParameter parameter;
	const fang::TypeInfo&   typeInfo = ReflectionTestParameter::GetTypeInfo();

	CHECK(typeInfo.TrySetField(&parameter, "speed", fang::FieldValue::MakeFloat(4.0f)));
	CHECK(parameter.speed == doctest::Approx(4.0f));

	fang::FieldValue readValue;
	CHECK(typeInfo.TryGetField(&parameter, "speed", &readValue));
	CHECK(readValue.type == fang::EnFieldType::Float);
	CHECK(readValue.floatValue == doctest::Approx(4.0f));

	CHECK(typeInfo.TrySetField(&parameter, "count", fang::FieldValue::MakeInt32(7)));
	CHECK(parameter.count == 7);

	CHECK(typeInfo.TrySetField(&parameter, "isActive", fang::FieldValue::MakeBool(true)));
	CHECK(parameter.isActive == true);
}


TEST_CASE("Reflection: 型が違えば false を返し、値も書かない")
{
	ReflectionTestParameter parameter;
	const fang::TypeInfo&   typeInfo = ReflectionTestParameter::GetTypeInfo();

	CHECK_FALSE(typeInfo.TrySetField(&parameter, "speed", fang::FieldValue::MakeInt32(9)));
	CHECK(parameter.speed == doctest::Approx(1.0f));

	CHECK_FALSE(typeInfo.TrySetField(&parameter, "count", fang::FieldValue::MakeBool(true)));
	CHECK(parameter.count == 0);
}


TEST_CASE("Reflection: 範囲を持つフィールドは書き込み時に丸める")
{
	ReflectionTestParameter parameter;
	const fang::TypeInfo&   typeInfo = ReflectionTestParameter::GetTypeInfo();

	CHECK(typeInfo.TrySetField(&parameter, "speed", fang::FieldValue::MakeFloat(100.0f)));
	CHECK(parameter.speed == doctest::Approx(10.0f));

	CHECK(typeInfo.TrySetField(&parameter, "speed", fang::FieldValue::MakeFloat(-5.0f)));
	CHECK(parameter.speed == doctest::Approx(0.0f));

	// 範囲を持たないフィールドは丸めない。
	CHECK(typeInfo.TrySetField(&parameter, "count", fang::FieldValue::MakeInt32(12345)));
	CHECK(parameter.count == 12345);
}


TEST_CASE("Reflection: 未知の名前は false を返す")
{
	ReflectionTestParameter parameter;
	const fang::TypeInfo&   typeInfo = ReflectionTestParameter::GetTypeInfo();

	fang::FieldValue readValue;
	CHECK_FALSE(typeInfo.TryGetField(&parameter, "unknownField", &readValue));
	CHECK_FALSE(typeInfo.TrySetField(&parameter, "unknownField", fang::FieldValue::MakeFloat(1.0f)));
	CHECK(typeInfo.FindField("unknownField") == nullptr);
}


TEST_CASE("Reflection: 入れ子（Struct）は名前で読み書きできない")
{
	ReflectionTestNestedParameter parameter;
	parameter.inner.speed = 3.0f;

	const fang::TypeInfo& typeInfo = ReflectionTestNestedParameter::GetTypeInfo();

	const fang::FieldInfo* innerField = typeInfo.FindField("inner");
	CHECK(innerField != nullptr);
	if (innerField != nullptr)
	{
		CHECK(innerField->type == fang::EnFieldType::Struct);
		CHECK(innerField->getNestedTypeInfo != nullptr);
	}

	fang::FieldValue readValue;
	CHECK_FALSE(typeInfo.TryGetField(&parameter, "inner", &readValue));
	CHECK_FALSE(typeInfo.TrySetField(&parameter, "inner", fang::FieldValue::MakeFloat(9.0f)));

	// 実体が 1 バイトも変わっていないことを、中のフィールドを直接読んで確かめる。
	CHECK(parameter.inner.speed == doctest::Approx(3.0f));
}


TEST_CASE("Reflection: アドレスで読み書きする ReadFieldValue / WriteFieldValue")
{
	float value = 1.0f;

	fang::FieldValue readValue;
	CHECK(fang::ReadFieldValue(&value, fang::EnFieldType::Float, &readValue));
	CHECK(readValue.floatValue == doctest::Approx(1.0f));

	CHECK(
		fang::WriteFieldValue(
			&value,
			fang::EnFieldType::Float,
			fang::Range(0.0f, 10.0f),
			fang::FieldValue::MakeFloat(100.0f)
		)
	);
	CHECK(value == doctest::Approx(10.0f));

	// Struct は値として読み書きできない。
	CHECK_FALSE(fang::ReadFieldValue(&value, fang::EnFieldType::Struct, &readValue));
	CHECK_FALSE(
		fang::WriteFieldValue(&value, fang::EnFieldType::Struct, fang::Range(), fang::FieldValue::MakeFloat(1.0f))
	);
}
