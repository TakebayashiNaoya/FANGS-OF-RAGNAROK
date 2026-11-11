/**
 * @file TextureData.h
 * @brief テクスチャのデータをまとめた構造体を定義するヘッダーファイル
 */
#pragma once
#include <d3d12.h>
#include <vector>
#include <filesystem>


namespace fangEngine::render
{
	/**
	 * @brief テクスチャのデータをまとめた構造体
	 */
	struct TextureData
	{
		DXGI_FORMAT format{};             // テクスチャのフォーマット
		UINT        width{};              // テクスチャの幅
		UINT        height{};             // テクスチャの高さ
		UINT        mipCount{};           // ミップマップの数
		std::vector<std::uint8_t> pixels; // テクスチャのピクセルデータ
	};


	/**
	 * @brief テクスチャを読み込む
	 * @param path 読み込むテクスチャのパス
	 * @param textureData 読み込んだテクスチャのデータを格納する構造体
	 * @return 読み込めた場合はtrue、読み込めなかった場合はfalse
	 */
	bool LoadTexture(const std::filesystem::path& path, TextureData& textureData);
} // namespace fangEngine::render
