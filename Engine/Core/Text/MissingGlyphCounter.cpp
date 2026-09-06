/**
 * @file MissingGlyphCounter.cpp
 * @brief 代替グリフへ落ちたコードポイントの集計。
 */
#include "Pch.h"
#include "Core/Text/MissingGlyphCounter.h"


namespace fang
{
	namespace
	{
		constexpr char32_t MAXIMUM_BMP_CODE_POINT = 0xFFFF;

		/**
		 * @brief UTF-8 の先頭 1 文字を読む。
		 * @param outCodePoint 読めたコードポイント。
		 * @return 読んだバイト数。壊れたバイト列（続きバイトが無い・範囲外）なら 1（読み飛ばす分）。
		 */
		size_t DecodeUtf8CodePoint(std::string_view text, size_t offset, char32_t& outCodePoint)
		{
			const auto   byte      = static_cast<unsigned char>(text[offset]);
			const size_t remaining = text.size() - offset;

			auto continuationByte = [&](size_t index) -> int {
				if (index >= remaining)
				{
					return -1;
				}
				const auto value = static_cast<unsigned char>(text[offset + index]);
				return (value & 0xC0) == 0x80 ? (value & 0x3F) : -1;
			};

			if (byte < 0x80)
			{
				outCodePoint = byte;
				return 1;
			}

			if ((byte & 0xE0) == 0xC0 && remaining >= 2)
			{
				const int continuation = continuationByte(1);
				if (continuation >= 0)
				{
					outCodePoint = static_cast<char32_t>(((byte & 0x1F) << 6) | continuation);
					return 2;
				}
			}
			else if ((byte & 0xF0) == 0xE0 && remaining >= 3)
			{
				const int continuation1 = continuationByte(1);
				const int continuation2 = continuationByte(2);
				if (continuation1 >= 0 && continuation2 >= 0)
				{
					outCodePoint = static_cast<char32_t>(((byte & 0x0F) << 12) | (continuation1 << 6) | continuation2);
					return 3;
				}
			}
			else if ((byte & 0xF8) == 0xF0 && remaining >= 4)
			{
				const int continuation1 = continuationByte(1);
				const int continuation2 = continuationByte(2);
				const int continuation3 = continuationByte(3);
				if (continuation1 >= 0 && continuation2 >= 0 && continuation3 >= 0)
				{
					outCodePoint = static_cast<char32_t>(
						((byte & 0x07) << 18) | (continuation1 << 12) | (continuation2 << 6) | continuation3
					);
					return 4;
				}
			}

			// 壊れたバイト列。1 バイトだけ読み飛ばして先へ進む。
			outCodePoint = 0;
			return 1;
		}
	} // namespace


	MissingGlyphCounter& MissingGlyphCounter::GetInstance()
	{
		static MissingGlyphCounter s_instance;
		return s_instance;
	}


	void MissingGlyphCounter::SetCallback(MissingGlyphCallback callback, void* userData)
	{
		m_callback = callback;
		m_userData = userData;
	}


	void MissingGlyphCounter::NoteMissingGlyph(char32_t codePoint)
	{
		++m_totalMissingCount;

		if (codePoint > MAXIMUM_BMP_CODE_POINT)
		{
			++m_nonBasicPlaneCount;
			return;
		}

		if (IsJudged(codePoint))
		{
			return;
		}

		MarkJudged(codePoint);
		++m_distinctMissingCount;

		if (m_callback != nullptr)
		{
			m_callback(codePoint, m_userData);
		}
	}


	void MissingGlyphCounter::NoteText(std::string_view text, GlyphPresenceCallback isGlyphAvailable, void* userData)
	{
		size_t offset = 0;
		while (offset < text.size())
		{
			char32_t     codePoint     = 0;
			const size_t bytesConsumed = DecodeUtf8CodePoint(text, offset, codePoint);
			offset += bytesConsumed;

			if (codePoint > MAXIMUM_BMP_CODE_POINT)
			{
				++m_totalMissingCount;
				++m_nonBasicPlaneCount;
				continue;
			}

			if (IsJudged(codePoint))
			{
				// 判定済み（実在・欠字のどちらでも）なので、ビットのテストだけで次へ進む。
				continue;
			}

			const bool isAvailable = isGlyphAvailable != nullptr && isGlyphAvailable(codePoint, userData);

			MarkJudged(codePoint);

			if (!isAvailable)
			{
				++m_totalMissingCount;
				++m_distinctMissingCount;

				if (m_callback != nullptr)
				{
					m_callback(codePoint, m_userData);
				}
			}
		}
	}


	uint32_t MissingGlyphCounter::GetDistinctMissingCount() const
	{
		return m_distinctMissingCount;
	}


	uint32_t MissingGlyphCounter::GetTotalMissingCount() const
	{
		return m_totalMissingCount;
	}


	uint32_t MissingGlyphCounter::GetNonBasicPlaneCount() const
	{
		return m_nonBasicPlaneCount;
	}


	void MissingGlyphCounter::Reset()
	{
		m_judgedBits.fill(0);
		m_distinctMissingCount = 0;
		m_totalMissingCount    = 0;
		m_nonBasicPlaneCount   = 0;
	}


	bool MissingGlyphCounter::IsJudged(char32_t codePoint) const
	{
		return (m_judgedBits[codePoint >> 6] & (uint64_t{ 1 } << (codePoint & 0x3F))) != 0;
	}


	void MissingGlyphCounter::MarkJudged(char32_t codePoint)
	{
		m_judgedBits[codePoint >> 6] |= (uint64_t{ 1 } << (codePoint & 0x3F));
	}


	/**
	 * @brief ImFont::FindGlyph から呼ばれるフックの実体。
	 * @details 宣言は ThirdParty/imgui/imconfig.h 側にある（imgui は Core を参照しないので、
	 *          exe のリンク時にここへ解決される）。詳細は ThirdParty/imgui/FangChanges.md。
	 */
	void NoteMissingGlyph(unsigned int codePoint)
	{
		MissingGlyphCounter::GetInstance().NoteMissingGlyph(static_cast<char32_t>(codePoint));
	}
} // namespace fang
