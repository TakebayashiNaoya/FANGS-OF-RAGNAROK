/**
 * @file ReflectionTests.cpp
 * @brief フィールド名での読み書き、型違い、範囲の丸め、未知の名前を確かめる。
 */
#include "Core/Reflection/Reflection.h"
#include <doctest.h>


namespace
{
	/** @brief float / int32_t / bool の 3 種を持つ、テスト専用の反射対象。 */
	struct ReflectionTestParams
	{
		FANG_REFLECT_BEGIN(ReflectionTestParams)
		FANG_FIELD(speed, "速度", fang::Range(0.0f, 10.0f))
		FANG_FIELD(count, "個数")
		FANG_FIELD(isActive, "有効")
		FANG_REFLECT_END()

		float   speed    = 1.0f;
		int32_t count    = 0;
		bool    isActive = false;
	};
} // namespace


TEST_CASE("Reflection: フィールド名で読み書きできる")
{
	ReflectionTestParams  params;
	const fang::TypeInfo& typeInfo = ReflectionTestParams::GetTypeInfo();

	CHECK(typeInfo.TrySetField(&params, "speed", fang::FieldValue::MakeFloat(4.0f)));
	CHECK(params.speed == doctest::Approx(4.0f));

	fang::FieldValue readValue;
	CHECK(typeInfo.TryGetField(&params, "speed", &readValue));
	CHECK(readValue.type == fang::EnFieldType::Float);
	CHECK(readValue.floatValue == doctest::Approx(4.0f));

	CHECK(typeInfo.TrySetField(&params, "count", fang::FieldValue::MakeInt32(7)));
	CHECK(params.count == 7);

	CHECK(typeInfo.TrySetField(&params, "isActive", fang::FieldValue::MakeBool(true)));
	CHECK(params.isActive == true);
}


TEST_CASE("Reflection: 型が違えば false を返し、値も書かない")
{
	ReflectionTestParams  params;
	const fang::TypeInfo& typeInfo = ReflectionTestParams::GetTypeInfo();

	CHECK_FALSE(typeInfo.TrySetField(&params, "speed", fang::FieldValue::MakeInt32(9)));
	CHECK(params.speed == doctest::Approx(1.0f));

	CHECK_FALSE(typeInfo.TrySetField(&params, "count", fang::FieldValue::MakeBool(true)));
	CHECK(params.count == 0);
}


TEST_CASE("Reflection: 範囲を持つフィールドは書き込み時に丸める")
{
	ReflectionTestParams  params;
	const fang::TypeInfo& typeInfo = ReflectionTestParams::GetTypeInfo();

	CHECK(typeInfo.TrySetField(&params, "speed", fang::FieldValue::MakeFloat(100.0f)));
	CHECK(params.speed == doctest::Approx(10.0f));

	CHECK(typeInfo.TrySetField(&params, "speed", fang::FieldValue::MakeFloat(-5.0f)));
	CHECK(params.speed == doctest::Approx(0.0f));

	// 範囲を持たないフィールドは丸めない。
	CHECK(typeInfo.TrySetField(&params, "count", fang::FieldValue::MakeInt32(12345)));
	CHECK(params.count == 12345);
}


TEST_CASE("Reflection: 未知の名前は false を返す")
{
	ReflectionTestParams  params;
	const fang::TypeInfo& typeInfo = ReflectionTestParams::GetTypeInfo();

	fang::FieldValue readValue;
	CHECK_FALSE(typeInfo.TryGetField(&params, "unknownField", &readValue));
	CHECK_FALSE(typeInfo.TrySetField(&params, "unknownField", fang::FieldValue::MakeFloat(1.0f)));
	CHECK(typeInfo.FindField("unknownField") == nullptr);
}
