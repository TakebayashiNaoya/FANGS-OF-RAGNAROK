/**
 * @file TuningRow.h
 * @brief TypeInfo を辿って畳んだ、つまみ 1 個 = 1 行の列。
 */
#pragma once

#include "Core/Reflection/TypeInfo.h"
#include <cstdint>
#include <span>
#include <type_traits>


namespace fang
{
	/** @brief 辿る段の上限。登録した型の直下を 1 段目と数える。 */
	inline constexpr uint32_t MAX_TUNING_DEPTH = 3;

	/** @brief 1 回に組み立てられる行の上限。今の登録 5 件で 41 本。 */
	inline constexpr uint32_t MAX_TUNING_ROW_COUNT = 128;

	/**
	 * @brief FormatTuningRowPath の書き込み先に要る長さ（終端を含む）。
	 * @details 今いちばん長い識別子は returnSpeedCentimetersPerSecond（31 文字）。
	 *          3 段ぶんと区切りの「.」2 個で 95 文字 ➡ 余白を足して 128。
	 */
	inline constexpr uint32_t MAX_TUNING_PATH_LENGTH = 128;

	/** @brief 行のパスの 1 節。文字は借りるだけで写さない（FieldInfo の文字列リテラルを指す）。 */
	struct TuningRowSegment
	{
		const char* name        = nullptr;
		const char* displayName = nullptr;
	};

	/**
	 * @brief つまみ 1 個ぶんの行。ImGui を 1 つも知らない。
	 * @details パネルはこれを 1 対 1 でつまみへ写すだけ ➡ 行が正しければ画面も正しい。
	 */
	struct TuningRow
	{
		/** @brief 親から辿るパス。segments[depth - 1] がこの行自身。 */
		TuningRowSegment segments[MAX_TUNING_DEPTH];

		uint8_t  depth      = 0; /**< 1 = 登録した型の直下。上限は MAX_TUNING_DEPTH。 */
		uint32_t entryIndex = 0; /**< どの登録から出た行か。 */

		void*       address = nullptr; /**< 実体のフィールドの先頭。ここへ書けば実体が変わる。 */
		EnFieldType type    = EnFieldType::Float;
		Range       range;

		[[nodiscard]] const char* GetDisplayName() const { return segments[depth - 1].displayName; }
	};

	static_assert(std::is_trivially_copyable_v<TuningRow>, "TuningRow は写すだけの POD であること");

	/** @brief 組み立ての結果。打ち切りは黙って捨てず、ここで数える。 */
	struct TuningRowBuildResult
	{
		uint32_t rowCount               = 0; /**< outRows の先頭から詰めた本数。 */
		uint32_t depthLimitedFieldCount = 0; /**< 段の上限を超えたので出さなかったフィールドの数。 */
		uint32_t droppedRowCount        = 0; /**< outRows が足りずに出せなかった行の数。 */
	};

	/**
	 * @brief TypeInfo を辿って、つまみ 1 個 = 1 行の列へ畳む。
	 * @param object     実体の先頭。null なら 0 本。
	 * @param entryIndex 行に付ける登録の番号。
	 * @param outRows    書き込み先。足りなければ droppedRowCount に数えて打ち切る。
	 * @details ヒープを確保しない（可変長のものを 1 つも持たない）。再帰は MAX_TUNING_DEPTH で止まる。
	 * @threading 呼び出し側の持ち物しか触らない。object を書き換えないので、更新ジョブと並んで呼んでよい。
	 */
	[[nodiscard]] TuningRowBuildResult BuildTuningRows(
		const TypeInfo&      typeInfo,
		void*                object,
		uint32_t             entryIndex,
		std::span<TuningRow> outRows
	);

	/**
	 * @brief 行のパスを "occlusion.minimumDistanceCentimeters" の形へ書き出す。
	 * @return 終端を除いた文字数。outPath が足りなければ切り詰めて入る分だけ書く。
	 */
	uint32_t FormatTuningRowPath(const TuningRow& row, std::span<char> outPath);
} // namespace fang
