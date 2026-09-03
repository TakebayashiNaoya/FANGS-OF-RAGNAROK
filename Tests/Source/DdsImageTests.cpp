/**
 * @file DdsImageTests.cpp
 * @brief DDS 解析のテスト。ミップの段数と範囲、壊れたヘッダの拒否を確かめる。
 */
#include "Core/Platform/FileSystem.h"
#include "Resource/DdsImage.h"
#include "NonAsciiTestDirectory.h"
#include <doctest.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


namespace
{
	constexpr uint32_t DDS_MAGIC   = 0x20534444;
	constexpr uint32_t FOURCC_DX10 = 0x30315844;
	constexpr uint32_t DDPF_FOURCC = 0x00000004;

	constexpr uint32_t DXGI_R8G8B8A8_UNORM     = 28;
	constexpr uint32_t DXGI_R16_UNORM          = 56;
	constexpr uint32_t DXGI_BC7_UNORM_SRGB     = 99;
	constexpr uint32_t DDS_DIMENSION_TEXTURE2D = 3;

	/** @brief magic(4) + ヘッダ(124) + DX10 拡張(20)。 */
	constexpr size_t HEADER_TOTAL_SIZE = 148;


	/** @brief テスト用の DDS をバイト列で組み立てる入れ物。 */
	class DdsBuilder
	{
	public:
		DdsBuilder(uint32_t width, uint32_t height, uint32_t mipCount, uint32_t dxgiFormat)
		{
			m_bytes.resize(HEADER_TOTAL_SIZE, 0);

			WriteUint32(0, DDS_MAGIC);
			WriteUint32(4, 124); // dwSize
			WriteUint32(12, height);
			WriteUint32(16, width);
			WriteUint32(28, mipCount);
			WriteUint32(76, 32);          // ddspf.dwSize
			WriteUint32(80, DDPF_FOURCC); // ddspf.dwFlags
			WriteUint32(84, FOURCC_DX10); // ddspf.dwFourCC
			WriteUint32(128, dxgiFormat);
			WriteUint32(132, DDS_DIMENSION_TEXTURE2D);
			WriteUint32(140, 1); // arraySize
		}

		/** @brief 指定バイト数のピクセルを、値を変えながら足す。 */
		void AppendPixels(size_t sizeInBytes)
		{
			for (size_t index = 0; index < sizeInBytes; ++index)
			{
				m_bytes.push_back(static_cast<uint8_t>((m_bytes.size() + index) & 0xFF));
			}
		}

		/** @brief 任意の位置を上書きする。壊れたヘッダを作るために使う。 */
		void WriteUint32(size_t offset, uint32_t value) { std::memcpy(m_bytes.data() + offset, &value, 4); }

		/** @brief 末尾から削る。途中で切れたファイルを作るために使う。 */
		void Truncate(size_t newSize) { m_bytes.resize(newSize); }

		[[nodiscard]] std::span<const uint8_t> GetBytes() const { return m_bytes; }


	private:
		std::vector<uint8_t> m_bytes;
	};
} // namespace


TEST_CASE("RGBA8 の 3 段を読むと、段ごとの寸法と行のバイト数が半分ずつになる")
{
	// 4x4 ➡ 2x2 ➡ 1x1。サイズは 64 + 16 + 4。
	DdsBuilder builder(4, 4, 3, DXGI_R8G8B8A8_UNORM);
	builder.AppendPixels(64 + 16 + 4);

	fang::DdsImage image;
	CHECK(image.LoadFromMemory(builder.GetBytes()));
	CHECK(image.GetFormat() == fang::rhi::EnTextureFormat::RGBA8);

	const auto mipLevels = image.GetMipLevels();
	CHECK_EQ(mipLevels.size(), 3);
	if (mipLevels.size() != 3)
	{
		return;
	}

	CHECK(mipLevels[0].width == 4);
	CHECK(mipLevels[0].height == 4);
	CHECK(mipLevels[0].rowPitch == 16);
	CHECK(mipLevels[0].sizeInBytes == 64);

	CHECK(mipLevels[1].width == 2);
	CHECK(mipLevels[1].rowPitch == 8);
	CHECK(mipLevels[1].sizeInBytes == 16);

	CHECK(mipLevels[2].width == 1);
	CHECK(mipLevels[2].height == 1);
	CHECK(mipLevels[2].sizeInBytes == 4);
}


TEST_CASE("段の始まりが前の段の直後に来る")
{
	DdsBuilder builder(4, 4, 3, DXGI_R8G8B8A8_UNORM);
	builder.AppendPixels(64 + 16 + 4);

	fang::DdsImage image;
	CHECK(image.LoadFromMemory(builder.GetBytes()));

	const auto mipLevels = image.GetMipLevels();
	CHECK_EQ(mipLevels.size(), 3);
	if (mipLevels.size() != 3)
	{
		return;
	}

	const auto* base = static_cast<const uint8_t*>(mipLevels[0].pixels);
	CHECK(static_cast<const uint8_t*>(mipLevels[1].pixels) == base + 64);
	CHECK(static_cast<const uint8_t*>(mipLevels[2].pixels) == base + 64 + 16);
}


TEST_CASE("BC7 はブロック単位で数える")
{
	// 8x8 は 2x2 ブロック ➡ 行 32 バイト × 2 行 = 64。以降 4x4 / 2x2 / 1x1 は全部 1 ブロック = 16。
	DdsBuilder builder(8, 8, 4, DXGI_BC7_UNORM_SRGB);
	builder.AppendPixels(64 + 16 + 16 + 16);

	fang::DdsImage image;
	CHECK(image.LoadFromMemory(builder.GetBytes()));
	CHECK(image.GetFormat() == fang::rhi::EnTextureFormat::BC7Srgb);

	const auto mipLevels = image.GetMipLevels();
	CHECK_EQ(mipLevels.size(), 4);
	if (mipLevels.size() != 4)
	{
		return;
	}

	CHECK(mipLevels[0].rowPitch == 32);
	CHECK(mipLevels[0].sizeInBytes == 64);

	// 4 テクセル未満でも 1 ブロックぶんの大きさを持つ。ここを width * 4 で数えると壊れる。
	CHECK(mipLevels[2].width == 2);
	CHECK(mipLevels[2].rowPitch == 16);
	CHECK(mipLevels[3].width == 1);
	CHECK(mipLevels[3].sizeInBytes == 16);
}


TEST_CASE("R16 は 1 テクセル 2 バイトで数える")
{
	// 4x4 の 1 段。ハイトマップはミップを持たない前提の形。
	DdsBuilder builder(4, 4, 1, DXGI_R16_UNORM);
	builder.AppendPixels(4 * 4 * 2);

	fang::DdsImage image;
	CHECK(image.LoadFromMemory(builder.GetBytes()));
	CHECK(image.GetFormat() == fang::rhi::EnTextureFormat::R16);

	const auto mipLevels = image.GetMipLevels();
	CHECK_EQ(mipLevels.size(), 1);
	if (mipLevels.size() != 1)
	{
		return;
	}

	CHECK(mipLevels[0].width == 4);
	CHECK(mipLevels[0].height == 4);
	CHECK(mipLevels[0].rowPitch == 8);
	CHECK(mipLevels[0].sizeInBytes == 32);
}


TEST_CASE("ミップ数 0 は 1 段として扱う")
{
	DdsBuilder builder(2, 2, 0, DXGI_R8G8B8A8_UNORM);
	builder.AppendPixels(16);

	fang::DdsImage image;
	CHECK(image.LoadFromMemory(builder.GetBytes()));
	CHECK_EQ(image.GetMipLevels().size(), 1);
}


TEST_CASE("壊れた DDS は読まず、中身が空のまま false を返す")
{
	SUBCASE("magic が違う")
	{
		DdsBuilder builder(4, 4, 1, DXGI_R8G8B8A8_UNORM);
		builder.AppendPixels(64);
		builder.WriteUint32(0, 0x12345678);

		fang::DdsImage image;
		CHECK_FALSE(image.LoadFromMemory(builder.GetBytes()));
		CHECK(image.GetMipLevels().empty());
	}

	SUBCASE("ヘッダの途中で切れている")
	{
		DdsBuilder builder(4, 4, 1, DXGI_R8G8B8A8_UNORM);
		builder.Truncate(100);

		fang::DdsImage image;
		CHECK_FALSE(image.LoadFromMemory(builder.GetBytes()));
	}

	SUBCASE("DX10 拡張ヘッダが無い（旧形式の DXT1 など）")
	{
		DdsBuilder builder(4, 4, 1, DXGI_R8G8B8A8_UNORM);
		builder.AppendPixels(64);
		builder.WriteUint32(84, 0x31545844); // "DXT1"

		fang::DdsImage image;
		CHECK_FALSE(image.LoadFromMemory(builder.GetBytes()));
	}

	SUBCASE("対応していない形式")
	{
		DdsBuilder builder(4, 4, 1, DXGI_R8G8B8A8_UNORM);
		builder.AppendPixels(64);
		builder.WriteUint32(128, 71); // DXGI_FORMAT_BC1_UNORM

		fang::DdsImage image;
		CHECK_FALSE(image.LoadFromMemory(builder.GetBytes()));
	}

	SUBCASE("ピクセルがミップの途中で切れている")
	{
		DdsBuilder builder(4, 4, 3, DXGI_R8G8B8A8_UNORM);
		builder.AppendPixels(64 + 16); // 3 段目が丸ごと無い

		fang::DdsImage image;
		CHECK_FALSE(image.LoadFromMemory(builder.GetBytes()));
		CHECK(image.GetMipLevels().empty());
	}

	SUBCASE("寸法が 0")
	{
		DdsBuilder builder(0, 4, 1, DXGI_R8G8B8A8_UNORM);

		fang::DdsImage image;
		CHECK_FALSE(image.LoadFromMemory(builder.GetBytes()));
	}
}


TEST_CASE("存在しないファイルは false を返して落ちない")
{
	fang::DdsImage image;
	CHECK_FALSE(image.Load("Z:\\存在しない\\Wolf.dds"));
	CHECK(image.GetMipLevels().empty());
}


TEST_CASE("非 ASCII を含むパスの DDS を読める")
{
	fang::test::NonAsciiTestDirectory directory(L"テクスチャ読み込みテスト_日本語パス");
	const std::string                 path = directory.MakeFilePath("テクスチャ.dds");

	DdsBuilder builder(4, 4, 1, DXGI_R8G8B8A8_UNORM);
	builder.AppendPixels(64);

	const auto bytes = builder.GetBytes();
	std::FILE* file  = fang::OpenFile(path.c_str(), "wb");
	CHECK(file != nullptr);
	if (file == nullptr)
	{
		return;
	}

	CHECK(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
	std::fclose(file);

	fang::DdsImage image;
	CHECK(image.Load(path.c_str()));
	CHECK(image.GetFormat() == fang::rhi::EnTextureFormat::RGBA8);
	CHECK(image.GetMipLevels().size() == 1);
}
