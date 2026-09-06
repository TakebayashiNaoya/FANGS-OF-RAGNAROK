/**
 * @file MissingGlyphCounterTests.cpp
 * @brief 代替グリフへ落ちたコードポイントの集計を確かめる。
 */
#include "Core/Text/MissingGlyphCounter.h"
#include <doctest.h>


namespace
{
	/** @brief 呼ばれた回数と、最後に渡されたコードポイントを控える。 */
	struct RecordingState
	{
		int      callCount     = 0;
		char32_t lastCodePoint = 0;
	};

	void RecordMissingGlyph(char32_t codePoint, void* userData)
	{
		auto* state = static_cast<RecordingState*>(userData);
		++state->callCount;
		state->lastCodePoint = codePoint;
	}

	/** @brief ASCII だけを「ある」と答える。呼ばれた回数も数える。 */
	struct AvailabilityCallCounter
	{
		int callCount = 0;
	};

	bool IsAsciiAvailable(char32_t codePoint, void* userData)
	{
		if (userData != nullptr)
		{
			++static_cast<AvailabilityCallCounter*>(userData)->callCount;
		}
		return codePoint < 0x80;
	}

	/** @brief 各 TEST_CASE の前に呼ぶ。前のテストの印を持ち越さない。 */
	fang::MissingGlyphCounter& GetResetCounter()
	{
		fang::MissingGlyphCounter& counter = fang::MissingGlyphCounter::GetInstance();
		counter.Reset();
		counter.SetCallback(nullptr, nullptr);
		return counter;
	}
} // namespace


TEST_CASE("MissingGlyphCounter: 同じ欠字の2回目はコールバックを呼ばない")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	RecordingState state;
	counter.SetCallback(&RecordMissingGlyph, &state);

	counter.NoteMissingGlyph(U'俯');
	counter.NoteMissingGlyph(U'俯');

	CHECK(counter.GetDistinctMissingCount() == 1);
	CHECK(counter.GetTotalMissingCount() == 2);
	CHECK(state.callCount == 1);
	CHECK(state.lastCodePoint == static_cast<char32_t>(U'俯'));
}


TEST_CASE("MissingGlyphCounter: 違う欠字はそれぞれ種類数を増やす")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	counter.NoteMissingGlyph(U'掴');
	counter.NoteMissingGlyph(U'繋');

	CHECK(counter.GetDistinctMissingCount() == 2);
	CHECK(counter.GetTotalMissingCount() == 2);
}


TEST_CASE("MissingGlyphCounter: NoteText はフォントにある字なら欠字を出さない")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	counter.NoteText("abc", &IsAsciiAvailable, nullptr);

	CHECK(counter.GetDistinctMissingCount() == 0);
	CHECK(counter.GetTotalMissingCount() == 0);
}


TEST_CASE("MissingGlyphCounter: NoteText は UTF-8 の複数バイト文字を1コードポイントとして解く")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	RecordingState state;
	counter.SetCallback(&RecordMissingGlyph, &state);

	// 「俯角」は 2 文字とも ASCII 外 ➡ IsAsciiAvailable がどちらも「無い」と答える。
	counter.NoteText("俯角", &IsAsciiAvailable, nullptr);

	CHECK(counter.GetDistinctMissingCount() == 2);
	CHECK(counter.GetTotalMissingCount() == 2);
	CHECK(state.callCount == 2);
}


TEST_CASE("MissingGlyphCounter: NoteText は判定済みの字を2回目以降は問い合わせない")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	AvailabilityCallCounter availabilityCallCounter;
	counter.NoteText("aa", &IsAsciiAvailable, &availabilityCallCounter);

	// 2 文字目の 'a' は 1 文字目で判定済み（ビットが立っている）なので問い合わせない。
	CHECK(availabilityCallCounter.callCount == 1);
}


TEST_CASE("MissingGlyphCounter: BMP外は種類ごとに数えず、コールバックも呼ばない")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	RecordingState state;
	counter.SetCallback(&RecordMissingGlyph, &state);

	// U+1F600（絵文字）を UTF-8 で 2 回。ImWchar が 16 bit で描けないので延べ数だけ数える。
	counter.NoteText("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", &IsAsciiAvailable, nullptr);

	CHECK(counter.GetNonBasicPlaneCount() == 2);
	CHECK(counter.GetDistinctMissingCount() == 0);
	CHECK(counter.GetTotalMissingCount() == 2);
	CHECK(state.callCount == 0);
}


TEST_CASE("MissingGlyphCounter: Resetは集計と判定済みの印を両方消す")
{
	fang::MissingGlyphCounter& counter = GetResetCounter();

	counter.NoteMissingGlyph(U'俯');
	counter.Reset();

	CHECK(counter.GetDistinctMissingCount() == 0);
	CHECK(counter.GetTotalMissingCount() == 0);
	CHECK(counter.GetNonBasicPlaneCount() == 0);

	// 印も消えているはずなので、同じ字をもう一度通知すると初出として扱われる。
	RecordingState state;
	counter.SetCallback(&RecordMissingGlyph, &state);
	counter.NoteMissingGlyph(U'俯');

	CHECK(counter.GetDistinctMissingCount() == 1);
	CHECK(state.callCount == 1);
}
