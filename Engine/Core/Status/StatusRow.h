/**
 * @file StatusRow.h
 * @brief 実行中の値を読み取り専用で並べる行と、固定長の列。
 */
#pragma once

#include <cstdint>
#include <span>
#include <type_traits>


namespace fang
{
	/** @brief 行の見せ方。パネルはこれで switch するだけで、他の判断を持たない。 */
	enum class EnStatusRowKind : uint8_t
	{
		Separator, /**< 区切り線。label は使わない。 */
		Scalar,    /**< 「倍率: 1.15」。 */
		Count,     /**< 「雑魚の生存数: 3 / 32」。 */
		Gauge,     /**< 「狼 1: 210 / 300 (70 %) 操作中」。 */
	};

	/**
	 * @brief 読み取り専用の値 1 行。ImGui を 1 つも知らない。
	 * @details label は文字列リテラルを借りるだけで写さない。ordinal が 0 でなければ label の後ろに番号を付ける。
	 */
	struct StatusRow
	{
		const char*     label    = nullptr;
		uint32_t        ordinal  = 0; /**< 0 = 番号なし。狼の席は 1 始まり。 */
		float           value    = 0.0f;
		float           maximum  = 0.0f; /**< Count / Gauge だけ使う。 */
		EnStatusRowKind kind     = EnStatusRowKind::Scalar;
		bool            isMarked = false; /**< Gauge の末尾に「操作中」を付ける。 */
	};

	static_assert(std::is_trivially_copyable_v<StatusRow>, "StatusRow は写すだけの POD であること");

	/**
	 * @brief 1 周ぶんの行の列。固定長で、溢れは数える。
	 * @details フレームメモリに置いて FrameData で描画へ渡す。作った側（更新ジョブ）だけが Append し、
	 *          描画は GetRows で読むだけ。
	 * @threading 書くのは作った更新ジョブ 1 本。読むのはその面を受け取った描画（メイン）。同じ面を同時に触る者はいない。
	 */
	struct StatusRowList
	{
		/** @brief 1 周に出せる行の上限。今は狼 9 + 固定 8 の 17 本。 */
		static constexpr uint32_t MAX_ROW_COUNT = 32;

		StatusRow rows[MAX_ROW_COUNT];
		uint32_t  rowCount        = 0;
		uint32_t  droppedRowCount = 0; /**< 上限で出せなかった本数。黙って捨てない。 */

		/** @return 満杯なら false（droppedRowCount が増える）。 */
		[[nodiscard]] bool Append(const StatusRow& row);

		[[nodiscard]] std::span<const StatusRow> GetRows() const;
	};

	static_assert(std::is_trivially_destructible_v<StatusRowList>, "NewFrame で置けること");

	/** @brief FormatStatusRow の書き込み先に要る長さ（終端を含む）。 */
	inline constexpr uint32_t MAX_STATUS_TEXT_LENGTH = 96;

	/**
	 * @brief 行を 1 本の文字列にする。Separator は空文字列。
	 * @param outText 書き込み先。足りなければ切り詰めて入る分だけ書く。
	 * @return 終端を除いた文字数。
	 * @details Gauge の割合は maximum が 0 以下なら 0 %。std::snprintf で組み、ヒープを使わない。
	 */
	[[nodiscard]] uint32_t FormatStatusRow(const StatusRow& row, std::span<char> outText);
} // namespace fang
