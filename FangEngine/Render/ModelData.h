/**
 * @file ModelData.h
 * @brief モデルのデータをまとめた構造体を定義するヘッダーファイル
 */
#pragma once
#include <vector>
#include <d3d12.h>
#include <filesystem>


namespace fangEngine::render
{
	/**
	 * @brief モデルの頂点情報をまとめた構造体
	 * @details LoadModel()は.fmodelの1頂点32バイトをこの構造体へそのまま写すため、メンバの並びは.fmodelの並び（位置、法線、UV）と同じにする。
	 */
	struct ModelVertex
	{
		float position[3]; // 頂点の位置
		float normal[3];   // 頂点の法線
		float uv[2];       // 頂点のUV座標
	};


	/**
	 * @brief メッシュのデータをまとめた構造体
	 * @details 頂点の配列、インデックスの配列、インデックスのフォーマット、インデックスの数を持つ。
	 */
	struct MeshData
	{
		std::vector<ModelVertex>  vertices;      // 頂点の配列
		std::vector<std::uint8_t> indices;       // インデックスの配列
		DXGI_FORMAT               indexFormat{}; // インデックスのフォーマット
		UINT                      indexCount{};  // インデックスの数
	};


	/**
	 * @brief モデルのデータをまとめた構造体
	 * @details メッシュの配列を持つ。
	 */
	struct ModelData
	{
		std::vector<MeshData> meshes; // メッシュの配列
	};


	/**
	 * @brief モデルを読み込む
	 * @param path モデルのファイルパス
	 * @param modelData 読み込んだモデルのデータを格納する構造体
	 * @return モデルを読み込めたらtrue、読み込めなかったらfalse。
	 */
	bool LoadModel(const std::filesystem::path& path, ModelData& modelData);
} // namespace fangEngine::render
