/**
 * @file MissingGlyphCounter.h
 * @brief 描画フォントに無く代替グリフへ落ちたコードポイントを数える。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <array>
#include <cstdint>
#include <string_view>


namespace fang
{
	/**
	 * @brief 代替グリフへ落ちたコードポイントを数える、プロセスに 1 つの集計係。
	 * @details 描画ライブラリ（ImGui）を 1 つも知らない。「落ちた」という事実だけを受け取る。
	 *          BMP（U+0000〜U+FFFF）は 1 字ずつ判定済みかどうかをビットで持ち、2 回目以降は
	 *          コールバックを呼ばない。BMP 外は種類ごとに覚えず、件数だけ数える。
	 * @threading メインスレッドのみ（ImGui の描画がメインに限られるため）。
	 */
	class MissingGlyphCounter
	{
	public:
		FANG_NON_COPYABLE(MissingGlyphCounter);
		FANG_NON_MOVABLE(MissingGlyphCounter);

		/** @brief 欠字を見つけたときに 1 回だけ呼ばれる。 */
		using MissingGlyphCallback = void (*)(char32_t codePoint, void* userData);

		/** @brief コードポイントがフォントにあるかを答える。NoteText がまだ描いていない字を調べるために呼ぶ。 */
		using GlyphPresenceCallback = bool (*)(char32_t codePoint, void* userData);

		MissingGlyphCounter()  = default;
		~MissingGlyphCounter() = default;

		/** @brief プロセスで 1 つの集計係。 */
		[[nodiscard]] static MissingGlyphCounter& GetInstance();

		/** @brief 欠字を見つけたときの通知先を差す。既定は無し（何も呼ばない）。 */
		void SetCallback(MissingGlyphCallback callback, void* userData);

		/**
		 * @brief 1 字が代替グリフへ落ちたことを記録する。
		 * @details 延べ数は毎回足す。同じ字の 2 回目以降は種類数を増やさず、コールバックも呼ばない。
		 */
		void NoteMissingGlyph(char32_t codePoint);

		/**
		 * @brief まだ描いていない文字列を先に調べる。
		 * @param text UTF-8。壊れたバイト列は読み飛ばす。
		 * @param isGlyphAvailable BMP のコードポイントごとに、判定済みでなければ呼ぶ。null なら「無い」扱い。
		 */
		void NoteText(std::string_view text, GlyphPresenceCallback isGlyphAvailable, void* userData);

		/** @return 欠字の種類数（同じ字は 1 回だけ数える）。 */
		[[nodiscard]] uint32_t GetDistinctMissingCount() const;

		/** @return 欠字の延べ数。 */
		[[nodiscard]] uint32_t GetTotalMissingCount() const;

		/** @return BMP 外（U+10000 以上）で落ちた延べ数。ImWchar が 16 bit なので種類は数えない。 */
		[[nodiscard]] uint32_t GetNonBasicPlaneCount() const;

		/** @brief 集計と判定済みビットを空にする。通知先は変えない。 */
		void Reset();


	private:
		/** @brief BMP 1 面ぶんの判定済みビット。65,536 ビット = 8 KiB。ヒープは確保しない。 */
		std::array<uint64_t, 1024> m_judgedBits{};

		MissingGlyphCallback m_callback             = nullptr;
		void*                m_userData             = nullptr;
		uint32_t             m_distinctMissingCount = 0;
		uint32_t             m_totalMissingCount    = 0;
		uint32_t             m_nonBasicPlaneCount   = 0;

		[[nodiscard]] bool IsJudged(char32_t codePoint) const;
		void               MarkJudged(char32_t codePoint);
	};
} // namespace fang
