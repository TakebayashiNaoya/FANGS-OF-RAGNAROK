/**
 * @file TuningRowTests.cpp
 * @brief TypeInfo を行の列へ畳む処理と、登録簿・控えた書き戻しを確かめる。
 */
#include "Core/Reflection/Reflection.h"
#include "Core/Reflection/TuningRegistry.h"
#include "Core/Reflection/TuningRow.h"
#include <doctest.h>
#include <array>
#include <cstring>


namespace
{
	/** @brief 直下に float を 2 個持つだけの、テスト専用の反射対象。 */
	struct TuningTestLeaf
	{
		FANG_REFLECT_BEGIN(TuningTestLeaf)
		FANG_FIELD(alpha, "アルファ", fang::Range(0.0f, 10.0f))
		FANG_FIELD(beta, "ベータ")
		FANG_REFLECT_END()

		float alpha = 1.0f;
		float beta  = 2.0f;
	};

	/** @brief float 1 個と、FANG_FIELD にしていない float 1 個を持つ。反映していないメンバの確認用。 */
	struct TuningTestWithUnreflectedMember
	{
		FANG_REFLECT_BEGIN(TuningTestWithUnreflectedMember)
		FANG_FIELD(alpha, "アルファ")
		FANG_REFLECT_END()

		float alpha        = 1.0f;
		float notReflected = 5.0f;
	};

	/** @brief 直下の float と、入れ子（TuningTestLeaf）を 1 個持つ。深さ 2 の確認用。 */
	struct TuningTestOneNested
	{
		FANG_REFLECT_BEGIN(TuningTestOneNested)
		FANG_FIELD(top, "トップ")
		FANG_FIELD_NESTED(nested, "入れ子")
		FANG_REFLECT_END()

		float          top = 1.0f;
		TuningTestLeaf nested;
	};

	/** @brief 深さ 4 段目に当たる型。ここへは 1 行も出ない。 */
	struct TuningTestLevel4
	{
		FANG_REFLECT_BEGIN(TuningTestLevel4)
		FANG_FIELD(deepValue, "深い値")
		FANG_REFLECT_END()

		float deepValue = 4.0f;
	};

	/** @brief 深さ 3 段目。直下の float と、4 段目に当たる入れ子を持つ。 */
	struct TuningTestLevel3
	{
		FANG_REFLECT_BEGIN(TuningTestLevel3)
		FANG_FIELD(value, "値", fang::Range(0.0f, 10.0f))
		FANG_FIELD_NESTED(level4, "レベル4")
		FANG_REFLECT_END()

		float            value = 3.0f;
		TuningTestLevel4 level4;
	};

	/** @brief 深さ 2 段目。 */
	struct TuningTestLevel2
	{
		FANG_REFLECT_BEGIN(TuningTestLevel2)
		FANG_FIELD_NESTED(level3, "レベル3")
		FANG_REFLECT_END()

		TuningTestLevel3 level3;
	};

	/** @brief 登録する側。直下（1 段目）は入れ子 1 個だけ。 */
	struct TuningTestLevel1
	{
		FANG_REFLECT_BEGIN(TuningTestLevel1)
		FANG_FIELD_NESTED(level2, "レベル2")
		FANG_REFLECT_END()

		TuningTestLevel2 level2;
	};


	/** @brief 各テストの前後で呼び、プロセス共有の登録簿を空にする。 */
	struct TuningRegistryTestScope
	{
		TuningRegistryTestScope() { fang::TuningRegistry::GetInstance().Clear(); }
		~TuningRegistryTestScope() { fang::TuningRegistry::GetInstance().Clear(); }
	};
} // namespace


TEST_CASE("TuningRow: 直下に float 2 個 ➡ 行 2 本")
{
	TuningTestLeaf                   instance;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result = fang::BuildTuningRows(TuningTestLeaf::GetTypeInfo(), &instance, 0, rows);

	CHECK(result.rowCount == 2);
	CHECK(result.depthLimitedFieldCount == 0);
	CHECK(result.droppedRowCount == 0);

	CHECK(std::strcmp(rows[0].GetDisplayName(), "アルファ") == 0);
	CHECK(rows[0].depth == 1);
	CHECK(rows[0].type == fang::EnFieldType::Float);
	CHECK(rows[0].range.hasRange);
	CHECK(rows[0].address == &instance.alpha);

	CHECK(std::strcmp(rows[1].GetDisplayName(), "ベータ") == 0);
	CHECK_FALSE(rows[1].range.hasRange);
	CHECK(rows[1].address == &instance.beta);

	char path[fang::MAX_TUNING_PATH_LENGTH];
	fang::FormatTuningRowPath(rows[0], path);
	CHECK(std::strcmp(path, "alpha") == 0);
}


TEST_CASE("TuningRow: 入れ子の中も行になり、パスが親から辿れる形になる")
{
	TuningTestOneNested              instance;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result =
		fang::BuildTuningRows(TuningTestOneNested::GetTypeInfo(), &instance, 0, rows);

	CHECK(result.rowCount == 3);

	CHECK(rows[0].depth == 1); // top
	CHECK(rows[1].depth == 2); // nested.alpha
	CHECK(rows[2].depth == 2); // nested.beta
	CHECK(rows[1].address == &instance.nested.alpha);

	char path[fang::MAX_TUNING_PATH_LENGTH];
	fang::FormatTuningRowPath(rows[1], path);
	CHECK(std::strcmp(path, "nested.alpha") == 0);
}


TEST_CASE("TuningRow: 3 段目まで行になり、4 段目は数えるだけで出さない")
{
	TuningTestLevel1                 instance;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result =
		fang::BuildTuningRows(TuningTestLevel1::GetTypeInfo(), &instance, 0, rows);

	// 出る行は level2.level3.value の 1 本だけ。level4 は段の上限で切られる。
	CHECK(result.rowCount == 1);
	CHECK(result.depthLimitedFieldCount == 1);
	CHECK(rows[0].depth == 3);
	CHECK(rows[0].address == &instance.level2.level3.value);

	char path[fang::MAX_TUNING_PATH_LENGTH];
	fang::FormatTuningRowPath(rows[0], path);
	CHECK(std::strcmp(path, "level2.level3.value") == 0);
}


TEST_CASE("TuningRow: 行に書くと実体が変わる（3 段目でも）")
{
	TuningTestLevel1                 instance;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result =
		fang::BuildTuningRows(TuningTestLevel1::GetTypeInfo(), &instance, 0, rows);
	CHECK(result.rowCount == 1);

	CHECK(fang::WriteFieldValue(rows[0].address, rows[0].type, rows[0].range, fang::FieldValue::MakeFloat(7.0f)));
	CHECK(instance.level2.level3.value == doctest::Approx(7.0f));
}


TEST_CASE("TuningRow: 範囲外の値は範囲に丸まって入る")
{
	TuningTestLeaf                   instance;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result = fang::BuildTuningRows(TuningTestLeaf::GetTypeInfo(), &instance, 0, rows);
	CHECK(result.rowCount == 2);

	CHECK(fang::WriteFieldValue(rows[0].address, rows[0].type, rows[0].range, fang::FieldValue::MakeFloat(100.0f)));
	CHECK(instance.alpha == doctest::Approx(10.0f));

	CHECK(fang::WriteFieldValue(rows[0].address, rows[0].type, rows[0].range, fang::FieldValue::MakeFloat(-5.0f)));
	CHECK(instance.alpha == doctest::Approx(0.0f));
}


TEST_CASE("TuningRow: 置き場が足りないと外へ書かず droppedRowCount に立つ")
{
	TuningTestLeaf                 instance;
	std::array<fang::TuningRow, 3> buffer{};

	// ガード。溢れた分がこの外側へこぼれていないことを後で確かめる。
	buffer[0].entryIndex = 0xDEADBEEFu;
	buffer[2].entryIndex = 0xDEADBEEFu;

	const fang::TuningRowBuildResult result =
		fang::BuildTuningRows(TuningTestLeaf::GetTypeInfo(), &instance, 0, std::span<fang::TuningRow>(&buffer[1], 1));

	CHECK(result.rowCount == 1);
	CHECK(result.droppedRowCount == 1);
	CHECK(buffer[0].entryIndex == 0xDEADBEEFu);
	CHECK(buffer[2].entryIndex == 0xDEADBEEFu);
}


TEST_CASE("TuningRow: FANG_FIELD にしていないメンバは行にならない")
{
	TuningTestWithUnreflectedMember  instance;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result =
		fang::BuildTuningRows(TuningTestWithUnreflectedMember::GetTypeInfo(), &instance, 0, rows);

	CHECK(result.rowCount == 1);
	CHECK(std::strcmp(rows[0].segments[0].name, "alpha") == 0);
}


TEST_CASE("TuningRow: object が null なら 0 本で落ちない")
{
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result = fang::BuildTuningRows(TuningTestLeaf::GetTypeInfo(), nullptr, 0, rows);
	CHECK(result.rowCount == 0);
}


TEST_CASE("TuningRow: フィールド 0 個の型を登録しても落ちない")
{
	// マクロでは要素 0 の配列を書けないので、TypeInfo を手で組む。
	const fang::TypeInfo emptyTypeInfo{ std::span<const fang::FieldInfo>{} };

	int                              dummyObject = 0;
	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result = fang::BuildTuningRows(emptyTypeInfo, &dummyObject, 0, rows);
	CHECK(result.rowCount == 0);
}


TEST_CASE("TuningRegistry: 登録 0 件でも落ちず 0 本で返る")
{
	TuningRegistryTestScope scope;

	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result = fang::TuningRegistry::GetInstance().BuildRows(rows);
	CHECK(result.rowCount == 0);
}


TEST_CASE("TuningRegistry: 実体のアドレスを持たない登録は弾かれる")
{
	TuningRegistryTestScope scope;
	fang::TuningRegistry&   registry = fang::TuningRegistry::GetInstance();

	CHECK_FALSE(registry.Register("名無し", nullptr, TuningTestLeaf::GetTypeInfo()));
	CHECK(registry.GetEntries().empty());
}


TEST_CASE("TuningRegistry: 同じ型を 2 か所に登録すると実体ごとに別の行が出る")
{
	TuningRegistryTestScope scope;
	fang::TuningRegistry&   registry = fang::TuningRegistry::GetInstance();

	TuningTestLeaf first;
	TuningTestLeaf second;
	CHECK(registry.Register("1 個目", &first));
	CHECK(registry.Register("2 個目", &second));

	std::array<fang::TuningRow, 8>   rows;
	const fang::TuningRowBuildResult result = registry.BuildRows(rows);
	CHECK(result.rowCount == 4);

	CHECK(rows[0].address != rows[2].address);

	CHECK(fang::WriteFieldValue(rows[0].address, rows[0].type, rows[0].range, fang::FieldValue::MakeFloat(9.0f)));
	CHECK(first.alpha == doctest::Approx(9.0f));
	CHECK(second.alpha == doctest::Approx(1.0f)); // 片方に書いてももう片方は変わらない。
}


TEST_CASE("TuningRegistry: 書いた値は EnqueueWrite の時点では実体に届かず、ApplyPendingWrites で届く")
{
	TuningRegistryTestScope scope;
	fang::TuningRegistry&   registry = fang::TuningRegistry::GetInstance();

	TuningTestLeaf instance;
	CHECK(registry.Register("対象", &instance));

	std::array<fang::TuningRow, 8> rows;
	CHECK(registry.BuildRows(rows).rowCount == 2);

	CHECK(registry.EnqueueWrite(rows[0], fang::FieldValue::MakeFloat(6.0f)));
	CHECK(instance.alpha == doctest::Approx(1.0f)); // 更新ジョブが読んでいる間はまだ書き換わらない。

	CHECK(registry.ApplyPendingWrites() == 1);
	CHECK(instance.alpha == doctest::Approx(6.0f));
}


TEST_CASE("TuningRegistry: 登録の件数が上限を超えたら失敗を返す")
{
	TuningRegistryTestScope scope;
	fang::TuningRegistry&   registry = fang::TuningRegistry::GetInstance();

	std::array<TuningTestLeaf, fang::TuningRegistry::MAX_ENTRY_COUNT + 1> instances;
	for (uint32_t index = 0; index < fang::TuningRegistry::MAX_ENTRY_COUNT; ++index)
	{
		CHECK(registry.Register("つまみ", &instances[index]));
	}

	CHECK_FALSE(registry.Register("溢れた 1 件", &instances[fang::TuningRegistry::MAX_ENTRY_COUNT]));
	CHECK(registry.GetEntries().size() == fang::TuningRegistry::MAX_ENTRY_COUNT);
}
