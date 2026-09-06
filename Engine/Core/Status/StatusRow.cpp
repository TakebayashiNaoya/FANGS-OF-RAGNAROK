/**
 * @file StatusRow.cpp
 * @brief StatusRowList の積み下ろしと、行を 1 本の文字列にする書式化。
 */
#include "Pch.h"
#include "Core/Status/StatusRow.h"
#include <cstdio>


namespace fang
{
	bool StatusRowList::Append(const StatusRow& row)
	{
		if (rowCount >= MAX_ROW_COUNT)
		{
			++droppedRowCount;
			return false;
		}

		rows[rowCount] = row;
		++rowCount;

		return true;
	}


	std::span<const StatusRow> StatusRowList::GetRows() const
	{
		return std::span<const StatusRow>(rows, rowCount);
	}


	uint32_t FormatStatusRow(const StatusRow& row, std::span<char> outText)
	{
		if (outText.empty())
		{
			return 0;
		}

		if (row.kind == EnStatusRowKind::Separator)
		{
			outText[0] = '\0';
			return 0;
		}

		// Gauge 以外は使わないが、0 除算を避けるため先に出しておく。
		const float percentage = row.maximum > 0.0f ? (row.value / row.maximum) * 100.0f : 0.0f;

		int written = 0;
		switch (row.kind)
		{
			case EnStatusRowKind::Scalar:
				written = std::snprintf(outText.data(), outText.size(), "%s: %.2f", row.label, row.value);
				break;

			case EnStatusRowKind::Count:
				written =
					std::snprintf(outText.data(), outText.size(), "%s: %.0f / %.0f", row.label, row.value, row.maximum);
				break;

			case EnStatusRowKind::Gauge:
				written = row.ordinal != 0 ? std::snprintf(
												 outText.data(),
												 outText.size(),
												 "%s %u: %.0f / %.0f (%.0f %%)%s",
												 row.label,
												 row.ordinal,
												 row.value,
												 row.maximum,
												 percentage,
												 row.isMarked ? " 操作中" : ""
											 )
										   : std::snprintf(
												 outText.data(),
												 outText.size(),
												 "%s: %.0f / %.0f (%.0f %%)%s",
												 row.label,
												 row.value,
												 row.maximum,
												 percentage,
												 row.isMarked ? " 操作中" : ""
											 );
				break;

			case EnStatusRowKind::Separator: break; // 上で return 済み。switch を割れさせないためだけに残す。
		}

		if (written < 0)
		{
			outText[0] = '\0';
			return 0;
		}

		const uint32_t writtenLength = static_cast<uint32_t>(written);
		const uint32_t capacity      = static_cast<uint32_t>(outText.size()) - 1; // 終端ぶんを引いた書ける長さ。

		return writtenLength < capacity ? writtenLength : capacity;
	}
} // namespace fang
