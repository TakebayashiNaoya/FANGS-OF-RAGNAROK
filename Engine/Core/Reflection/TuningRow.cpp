/**
 * @file TuningRow.cpp
 * @brief TypeInfo を辿って畳む、深さ優先の再帰 1 本。
 */
#include "Pch.h"
#include "Core/Reflection/TuningRow.h"


namespace fang
{
	namespace
	{
		/** @brief 1 段ぶんを処理し、Struct なら中へ降りる。降りた先で outRows へ詰める側の実体。 */
		void AppendRows(
			const TypeInfo&       typeInfo,
			std::byte*            object,
			uint32_t              entryIndex,
			uint8_t               depth,
			TuningRowSegment*     pathSegments, // 呼び出し側が持つ MAX_TUNING_DEPTH 本の配列
			std::span<TuningRow>  outRows,
			TuningRowBuildResult* result
		)
		{
			for (const FieldInfo& field : typeInfo.fields)
			{
				pathSegments[depth - 1] = TuningRowSegment{ field.name, field.displayName };

				if (field.type == EnFieldType::Struct)
				{
					// 4 段目に当たるフィールドは出さない。黙って消さず、切った数を数える。
					if (depth >= MAX_TUNING_DEPTH || field.getNestedTypeInfo == nullptr)
					{
						++result->depthLimitedFieldCount;
						continue;
					}

					AppendRows(
						field.getNestedTypeInfo(),
						object + field.offset,
						entryIndex,
						static_cast<uint8_t>(depth + 1),
						pathSegments,
						outRows,
						result
					);
					continue;
				}

				if (result->rowCount >= outRows.size())
				{
					++result->droppedRowCount;
					continue;
				}

				TuningRow& row = outRows[result->rowCount];
				row            = TuningRow{};
				for (uint8_t index = 0; index < depth; ++index)
				{
					row.segments[index] = pathSegments[index];
				}
				row.depth      = depth;
				row.entryIndex = entryIndex;
				row.address    = object + field.offset;
				row.type       = field.type;
				row.range      = field.range;

				++result->rowCount;
			}
		}
	} // namespace


	TuningRowBuildResult BuildTuningRows(
		const TypeInfo&      typeInfo,
		void*                object,
		uint32_t             entryIndex,
		std::span<TuningRow> outRows
	)
	{
		TuningRowBuildResult result;
		if (object == nullptr)
		{
			return result;
		}

		TuningRowSegment pathSegments[MAX_TUNING_DEPTH];
		AppendRows(typeInfo, static_cast<std::byte*>(object), entryIndex, 1, pathSegments, outRows, &result);
		return result;
	}


	uint32_t FormatTuningRowPath(const TuningRow& row, std::span<char> outPath)
	{
		if (outPath.empty())
		{
			return 0;
		}

		// 書き込み先が足りなければ、その場で切り詰めて入る分だけ書く（末尾は常に終端する）。
		size_t written = 0;
		for (uint8_t index = 0; index < row.depth && written < outPath.size() - 1; ++index)
		{
			if (index > 0 && written < outPath.size() - 1)
			{
				outPath[written] = '.';
				++written;
			}

			for (const char* character = row.segments[index].name; *character != '\0' && written < outPath.size() - 1;
				 ++character)
			{
				outPath[written] = *character;
				++written;
			}
		}

		outPath[written] = '\0';
		return static_cast<uint32_t>(written);
	}
} // namespace fang
