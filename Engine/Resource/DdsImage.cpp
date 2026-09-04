/**
 * @file DdsImage.cpp
 * @brief DDS ファイルの解析の実装。
 */
#include "Pch.h"
#include "Resource/DdsImage.h"
#include "Core/Platform/FileSystem.h"
#include "Resource/ResourceLog.h"
#include <cstdio>
#include <cstring>


namespace fang
{
	namespace
	{
		/** @brief ファイル先頭の 4 バイト。"DDS " をリトルエンディアンで読んだ値。 */
		constexpr uint32_t DDS_MAGIC = 0x20534444;

		/** @brief ヘッダの dwFourCC に入る "DX10"。これがあると拡張ヘッダが続く。 */
		constexpr uint32_t FOURCC_DX10 = 0x30315844;

		/** @brief ddspf.dwFlags の「dwFourCC が有効」ビット。 */
		constexpr uint32_t DDPF_FOURCC = 0x00000004;

		/** @brief DXGI_FORMAT の番号。d3d12.h を Resource へ持ち込まないため値で持つ。 */
		constexpr uint32_t DXGI_R8G8B8A8_UNORM      = 28;
		constexpr uint32_t DXGI_R8G8B8A8_UNORM_SRGB = 29;
		constexpr uint32_t DXGI_R16_UNORM           = 56;
		constexpr uint32_t DXGI_BC7_UNORM           = 98;
		constexpr uint32_t DXGI_BC7_UNORM_SRGB      = 99;

		/** @brief DX10 拡張ヘッダの resourceDimension が 2D テクスチャを指す値。 */
		constexpr uint32_t DDS_DIMENSION_TEXTURE2D = 3;

		/** @brief RHI 側の上限（TexturePool の MAX_MIP_COUNT）と同じ。16384 テクセルの全ミップでも 15 段。 */
		constexpr uint32_t MAX_MIP_COUNT = 16;

		/**
		 * @brief ヘッダの並び。DDS の仕様どおりの固定長で、memcpy で写して読む。
		 * @details 使わない欄も、後続のオフセットを合わせるために省略しない。
		 */
		struct DdsHeader
		{
			uint32_t size; /**< 124 であること。 */
			uint32_t flags;
			uint32_t height;
			uint32_t width;
			uint32_t pitchOrLinearSize;
			uint32_t depth;
			uint32_t mipMapCount;
			uint32_t reserved1[11];

			uint32_t pixelFormatSize; /**< 32 であること。 */
			uint32_t pixelFormatFlags;
			uint32_t fourCC;
			uint32_t rgbBitCount;
			uint32_t redMask;
			uint32_t greenMask;
			uint32_t blueMask;
			uint32_t alphaMask;

			uint32_t caps;
			uint32_t caps2;
			uint32_t caps3;
			uint32_t caps4;
			uint32_t reserved2;
		};
		static_assert(sizeof(DdsHeader) == 124, "DDS ヘッダは仕様で 124 バイト");

		/** @brief DX10 拡張ヘッダ。BC 系はこの形でしか書かれない。 */
		struct DdsHeaderDx10
		{
			uint32_t dxgiFormat;
			uint32_t resourceDimension;
			uint32_t miscFlag;
			uint32_t arraySize;
			uint32_t miscFlags2;
		};
		static_assert(sizeof(DdsHeaderDx10) == 20, "DX10 拡張ヘッダは仕様で 20 バイト");

		/** @brief magic + ヘッダ + 拡張ヘッダの合計。ピクセルはこの直後から始まる。 */
		constexpr size_t DATA_OFFSET = sizeof(uint32_t) + sizeof(DdsHeader) + sizeof(DdsHeaderDx10);

		/** @brief BC 系のブロックが受け持つテクセルの幅。 */
		constexpr uint32_t BLOCK_TEXEL_SIZE = 4;

		/** @brief BC7 のブロック 1 個のバイト数。 */
		constexpr uint32_t BC7_BLOCK_BYTE_SIZE = 16;

		/** @brief 1 テクセル 4 バイト（RGBA8）。 */
		constexpr uint32_t RGBA8_TEXEL_BYTE_SIZE = 4;

		/** @brief 1 テクセル 2 バイト（R16）。 */
		constexpr uint32_t R16_TEXEL_BYTE_SIZE = 2;

		[[nodiscard]] bool IsBlockCompressed(rhi::EnTextureFormat format)
		{
			return format == rhi::EnTextureFormat::BC7 || format == rhi::EnTextureFormat::BC7Srgb;
		}

		/**
		 * @brief 1 段ぶんの行のバイト数と行数を出す。
		 * @details ここの数え方は GetCopyableFootprints が返す値と一致していないといけない。
		 *          ずれていると TexturePool が転送前に弾く。
		 */
		void CalculateMipLayout(
			rhi::EnTextureFormat format,
			uint32_t             width,
			uint32_t             height,
			uint32_t*            outRowPitch,
			uint32_t*            outRowCount
		)
		{
			if (IsBlockCompressed(format))
			{
				// BC の「1 行」は 4 テクセル行ぶんのブロック列。端数はブロックごと切り上げる。
				const uint32_t blockCountX = (width + BLOCK_TEXEL_SIZE - 1) / BLOCK_TEXEL_SIZE;
				const uint32_t blockCountY = (height + BLOCK_TEXEL_SIZE - 1) / BLOCK_TEXEL_SIZE;

				*outRowPitch = blockCountX * BC7_BLOCK_BYTE_SIZE;
				*outRowCount = blockCountY;
				return;
			}

			const uint32_t texelByteSize =
				format == rhi::EnTextureFormat::R16 ? R16_TEXEL_BYTE_SIZE : RGBA8_TEXEL_BYTE_SIZE;

			*outRowPitch = width * texelByteSize;
			*outRowCount = height;
		}
	} // namespace


	bool DdsImage::Load(const char* filePath)
	{
		Clear();

		if (filePath == nullptr || filePath[0] == '\0')
		{
			FANG_LOG_ERROR(Resource, "DDS のパスが空");
			return false;
		}

		// narrow の fopen 系は現在の ANSI コードページでパスを解釈するため、日本語などの
		// 非 ASCII を含むパスが化ける。OpenFile が内部で UTF-16 に直してから開く。
		std::FILE* file = OpenFile(filePath, "rb");
		if (file == nullptr)
		{
			FANG_LOG_ERROR(Resource, "DDS を開けなかった: {}", filePath);
			return false;
		}

		std::fseek(file, 0, SEEK_END);
		const long fileSize = std::ftell(file);
		std::fseek(file, 0, SEEK_SET);

		bool isRead = false;
		if (fileSize > 0)
		{
			m_fileBytes.resize(static_cast<size_t>(fileSize));
			isRead = std::fread(m_fileBytes.data(), 1, m_fileBytes.size(), file) == m_fileBytes.size();
		}
		std::fclose(file);

		if (!isRead)
		{
			FANG_LOG_ERROR(Resource, "DDS を読み切れなかった: {}", filePath);
			Clear();
			return false;
		}

		if (!Parse())
		{
			FANG_LOG_ERROR(Resource, "DDS として読めなかった: {}", filePath);
			Clear();
			return false;
		}

		FANG_LOG_INFO(
			Resource,
			"DDS を読んだ: {}x{} / ミップ {} 段: {}",
			m_mipLevels[0].width,
			m_mipLevels[0].height,
			m_mipLevels.size(),
			filePath
		);

		return true;
	}


	bool DdsImage::LoadFromMemory(std::span<const uint8_t> bytes)
	{
		Clear();

		m_fileBytes.assign(bytes.begin(), bytes.end());
		if (!Parse())
		{
			Clear();
			return false;
		}

		return true;
	}


	bool DdsImage::Parse()
	{
		if (m_fileBytes.size() < DATA_OFFSET)
		{
			FANG_LOG_ERROR(Resource, "DDS のヘッダが足りない: {} バイト", m_fileBytes.size());
			return false;
		}

		uint32_t magic = 0;
		std::memcpy(&magic, m_fileBytes.data(), sizeof(magic));
		if (magic != DDS_MAGIC)
		{
			FANG_LOG_ERROR(Resource, "DDS の magic が違う");
			return false;
		}

		DdsHeader header{};
		std::memcpy(&header, m_fileBytes.data() + sizeof(magic), sizeof(header));
		if (header.size != sizeof(DdsHeader) || header.pixelFormatSize != 32)
		{
			FANG_LOG_ERROR(Resource, "DDS のヘッダの大きさが仕様と違う");
			return false;
		}

		if ((header.pixelFormatFlags & DDPF_FOURCC) == 0 || header.fourCC != FOURCC_DX10)
		{
			// 旧形式（マスク指定や DXT1 など）は読まない。texconv に -dx10 を付ければこの形になる。
			FANG_LOG_ERROR(Resource, "DX10 拡張ヘッダの無い DDS は読まない");
			return false;
		}

		DdsHeaderDx10 headerDx10{};
		std::memcpy(&headerDx10, m_fileBytes.data() + sizeof(magic) + sizeof(header), sizeof(headerDx10));

		switch (headerDx10.dxgiFormat)
		{
			case DXGI_R8G8B8A8_UNORM: m_format = rhi::EnTextureFormat::RGBA8; break;
			case DXGI_R8G8B8A8_UNORM_SRGB: m_format = rhi::EnTextureFormat::RGBA8Srgb; break;
			case DXGI_R16_UNORM: m_format = rhi::EnTextureFormat::R16; break;
			case DXGI_BC7_UNORM: m_format = rhi::EnTextureFormat::BC7; break;
			case DXGI_BC7_UNORM_SRGB: m_format = rhi::EnTextureFormat::BC7Srgb; break;
			default:
				FANG_LOG_ERROR(Resource, "対応していない DDS の形式: DXGI_FORMAT {}", headerDx10.dxgiFormat);
				return false;
		}

		if (headerDx10.resourceDimension != DDS_DIMENSION_TEXTURE2D || headerDx10.arraySize != 1)
		{
			FANG_LOG_ERROR(Resource, "2D テクスチャ 1 枚の DDS だけを読む");
			return false;
		}

		if (header.width == 0 || header.height == 0)
		{
			FANG_LOG_ERROR(Resource, "DDS の寸法が 0");
			return false;
		}

		// ミップ数 0 は「ミップ無し」の意味で書かれることがあるので 1 として扱う。
		const uint32_t mipCount = header.mipMapCount > 0 ? header.mipMapCount : 1;
		if (mipCount > MAX_MIP_COUNT)
		{
			FANG_LOG_ERROR(Resource, "DDS のミップ段数が多すぎる: {}", mipCount);
			return false;
		}

		m_mipLevels.reserve(mipCount);

		size_t   offset = DATA_OFFSET;
		uint32_t width  = header.width;
		uint32_t height = header.height;
		for (uint32_t mip = 0; mip < mipCount; ++mip)
		{
			uint32_t rowPitch = 0;
			uint32_t rowCount = 0;
			CalculateMipLayout(m_format, width, height, &rowPitch, &rowCount);

			const size_t mipSize = static_cast<size_t>(rowPitch) * rowCount;
			if (offset + mipSize > m_fileBytes.size())
			{
				FANG_LOG_ERROR(Resource, "DDS の中身がミップ {} の途中で切れている", mip);
				return false;
			}

			m_mipLevels.push_back(
				rhi::TextureMipLevel{
					.pixels      = m_fileBytes.data() + offset,
					.width       = width,
					.height      = height,
					.rowPitch    = rowPitch,
					.sizeInBytes = static_cast<uint32_t>(mipSize),
				}
			);

			offset += mipSize;
			width  = width > 1 ? width / 2 : 1;
			height = height > 1 ? height / 2 : 1;
		}

		return true;
	}


	void DdsImage::Clear()
	{
		// 範囲がバイト列を指しているので、指している側から先に捨てる。
		m_mipLevels.clear();
		m_fileBytes.clear();
	}
} // namespace fang
