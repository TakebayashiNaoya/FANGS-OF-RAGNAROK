/**
 * @file StatusRowTests.cpp
 * @brief StatusRowList（積む・溢れ・境界）と FormatStatusRow（4 種類の書式・切り詰め）のテスト。
 */
#include "Core/Status/StatusRow.h"
#include <doctest.h>
#include <cstring>


TEST_CASE("StatusRowList::Append: 1本積むと1本読める")
{
	fang::StatusRowList   list;
	const fang::StatusRow row{ .label = "倍率", .value = 1.15f, .kind = fang::EnStatusRowKind::Scalar };

	CHECK(list.Append(row));
	CHECK(list.rowCount == 1);
	CHECK(list.GetRows().size() == 1);
	CHECK(list.GetRows()[0].label == row.label);
	CHECK(list.GetRows()[0].value == doctest::Approx(1.15f));
}


TEST_CASE("StatusRowList::Append: 上限で止まり、溢れは数える")
{
	fang::StatusRowList list;
	for (uint32_t index = 0; index < fang::StatusRowList::MAX_ROW_COUNT; ++index)
	{
		CHECK(list.Append(fang::StatusRow{}));
	}

	CHECK_FALSE(list.Append(fang::StatusRow{}));
	CHECK(list.rowCount == fang::StatusRowList::MAX_ROW_COUNT);
	CHECK(list.droppedRowCount == 1);
}


TEST_CASE("StatusRowList::Append: 溢れが外へ書かれない")
{
	struct GuardedList
	{
		uint32_t            frontSentinel = 0xDEADBEEFu;
		fang::StatusRowList list;
		uint32_t            backSentinel = 0xDEADBEEFu;
	};

	GuardedList guarded;
	for (uint32_t index = 0; index < fang::StatusRowList::MAX_ROW_COUNT + 4; ++index)
	{
		(void)guarded.list.Append(fang::StatusRow{});
	}

	CHECK(guarded.frontSentinel == 0xDEADBEEFu);
	CHECK(guarded.backSentinel == 0xDEADBEEFu);
	CHECK(guarded.list.droppedRowCount == 4);
}


TEST_CASE("FormatStatusRow: Gaugeの文字列(番号あり・操作中)")
{
	const fang::StatusRow row{
		.label    = "狼",
		.ordinal  = 1,
		.value    = 210.0f,
		.maximum  = 300.0f,
		.kind     = fang::EnStatusRowKind::Gauge,
		.isMarked = true,
	};

	char           text[fang::MAX_STATUS_TEXT_LENGTH];
	const uint32_t length = fang::FormatStatusRow(row, text);

	CHECK(std::strcmp(text, "狼 1: 210 / 300 (70 %) 操作中") == 0);
	CHECK(length == std::strlen(text));
}


TEST_CASE("FormatStatusRow: Gaugeの文字列(番号なし・印なし)")
{
	const fang::StatusRow row{
		.label    = "狼",
		.ordinal  = 0,
		.value    = 210.0f,
		.maximum  = 300.0f,
		.kind     = fang::EnStatusRowKind::Gauge,
		.isMarked = false,
	};

	char text[fang::MAX_STATUS_TEXT_LENGTH];
	(void)fang::FormatStatusRow(row, text);

	CHECK(std::strcmp(text, "狼: 210 / 300 (70 %)") == 0);
}


TEST_CASE("FormatStatusRow: 上限0でも落ちない(0%になり、NaN/infが出ない)")
{
	const fang::StatusRow row{
		.label   = "狼",
		.ordinal = 0,
		.value   = 210.0f,
		.maximum = 0.0f,
		.kind    = fang::EnStatusRowKind::Gauge,
	};

	char text[fang::MAX_STATUS_TEXT_LENGTH];
	(void)fang::FormatStatusRow(row, text);

	CHECK(std::strcmp(text, "狼: 210 / 0 (0 %)") == 0);
}


TEST_CASE("FormatStatusRow: Countの文字列")
{
	const fang::StatusRow row{
		.label   = "雑魚の生存数",
		.value   = 3.0f,
		.maximum = 32.0f,
		.kind    = fang::EnStatusRowKind::Count,
	};

	char text[fang::MAX_STATUS_TEXT_LENGTH];
	(void)fang::FormatStatusRow(row, text);

	CHECK(std::strcmp(text, "雑魚の生存数: 3 / 32") == 0);
}


TEST_CASE("FormatStatusRow: Scalarの文字列")
{
	const fang::StatusRow row{ .label = "倍率", .value = 1.15f, .kind = fang::EnStatusRowKind::Scalar };

	char text[fang::MAX_STATUS_TEXT_LENGTH];
	(void)fang::FormatStatusRow(row, text);

	CHECK(std::strcmp(text, "倍率: 1.15") == 0);
}


TEST_CASE("FormatStatusRow: Separatorは空文字列")
{
	const fang::StatusRow row{ .kind = fang::EnStatusRowKind::Separator };

	char           text[fang::MAX_STATUS_TEXT_LENGTH] = { 'x', '\0' };
	const uint32_t length                             = fang::FormatStatusRow(row, text);

	CHECK(length == 0);
	CHECK(text[0] == '\0');
}


TEST_CASE("FormatStatusRow: 書き込み先が足りないと終端付きで切れる")
{
	const fang::StatusRow row{ .label   = "雑魚の生存数",
							   .value   = 3.0f,
							   .maximum = 32.0f,
							   .kind    = fang::EnStatusRowKind::Count };

	char           text[8] = { 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A' };
	const uint32_t length  = fang::FormatStatusRow(row, text);

	CHECK(length == 7);
	CHECK(text[7] == '\0');
	CHECK(std::strlen(text) == 7);
}


TEST_CASE("StatusRowList::GetRows: 空の列でも落ちない")
{
	const fang::StatusRowList list;
	CHECK(list.GetRows().empty());
}
