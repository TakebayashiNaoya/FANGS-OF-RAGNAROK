/**
 * @file TextureData.cpp
 * @brief テクスチャのデータをまとめた構造体を定義する実装
 */
#include "TextureData.h"
#include <fstream>
#include <cstring>
#include "Core/Log.h"


namespace fangEngine::render
{
	namespace
	{
		// DDSヘッダーの構造体（148バイト）
		struct DdsHeader
		{
			char magic[4]; // "DDS "の文字列

			// 画像の形式（72バイト）
			UINT size;              // この塊からreserved2までのバイト数（124）
			UINT flags;             // どの欄が有効か
			UINT height;            // テクスチャの高さ
			UINT width;             // テクスチャの幅
			UINT pitchOrLinearSize; // 1行(または1段目全体)のバイト数
			UINT depth;             // 3Dテクスチャの奥行き
			UINT mipMapCount;       // ミップマップの数
			UINT reserved1[11];     // 使わない

			// ピクセルの形式（32バイト）
			UINT pixelFormatSize;  // この塊のバイト数（32）
			UINT pixelFormatFlags; // どの欄が有効か
			char fourCC[4];        // 形式の名前（"DX10"ならDX10の拡張を見る）
			UINT rgbBitCount;      // 1ピクセルのビット数
			UINT rBitMask;         // 赤のビットの位置
			UINT gBitMask;         // 緑のビットの位置
			UINT bBitMask;         // 青のビットの位置
			UINT aBitMask;         // 透明度のビットの位置

			// 中身の種類（20バイト）
			UINT caps;      // ミップがあるか
			UINT caps2;     // キューブマップか、3Dテクスチャか
			UINT caps3;     // 使わない
			UINT caps4;     // 使わない
			UINT reserved2; // 使わない

			// DX10の拡張（20バイト）
			UINT dxgiFormat;        // DXGI_FORMATの値
			UINT resourceDimension; // 1D/2D/3D
			UINT miscFlag;          // キューブマップかどうか
			UINT arraySize;         // 配列の数
			UINT miscFlags2;        // アルファの扱い
		};
		static_assert(sizeof(DdsHeader) == 148);
	}


	bool LoadTexture(const std::filesystem::path& path, TextureData& textureData)
	{
		// ファイルを開く
		std::ifstream file(path, std::ios::binary);
		if (file.is_open() == false) {
			std::wstring message = L"テクスチャのファイルを開けませんでした。" + path.wstring();
			core::LogError(message.c_str());
			return false;
		}

		// ヘッダーの148バイトを読む
		DdsHeader header{};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));

		// エンジンが読める形か確かめる
		if (std::memcmp(header.magic, "DDS ", 4) != 0) { core::LogError(L"DDSのヘッダーが「DDS 」ではありません。"); return false; }
		if (std::memcmp(header.fourCC, "DX10", 4) != 0) { core::LogError(L"DDSのfourCCが「DX10」ではありません。"); return false; }
		if (header.resourceDimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) { core::LogError(L"DDSが2Dテクスチャではありません。"); return false; }
		if (header.arraySize != 1) { core::LogError(L"DDSの配列サイズが1ではありません。"); return false; }

		// 大きさと形式を写す
		textureData.format   = static_cast<DXGI_FORMAT>(header.dxgiFormat);
		textureData.width    = header.width;
		textureData.height   = header.height;
		textureData.mipCount = header.mipMapCount;

		// ヘッダーの後ろを全部読む
		textureData.pixels.resize(std::filesystem::file_size(path) - sizeof(header));
		file.read(reinterpret_cast<char*>(textureData.pixels.data()), textureData.pixels.size());

		// 途中で切れていないか確かめる
		if (file.fail()) { core::LogError(L"DDSのファイルが途中で切れています。"); return false; }

		return true;
	}
} // namespace fangEngine::render
