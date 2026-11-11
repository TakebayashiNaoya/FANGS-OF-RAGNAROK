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
	 * @details LoadModel()は.fmodelの1頂点40バイトをこの構造体へそのまま写すため、メンバの並びは.fmodelの並び（位置、法線、UV、2つ目のUV）と同じにする。
	 */
	struct ModelVertex
	{
		float position[3]; // 頂点の位置
		float normal[3];   // 頂点の法線
		float uv[2];       // 頂点のUV座標
		float secondUv[2]; // 頂点の2つ目のUV座標
	};


	/**
	 * @brief アルファモードの種類を表す列挙型
	 * @details Opaqueは不透明、Maskは透明度が半分より下の所を抜き、Blendは後ろの色と混ぜる。
	 *          .fmodelの値をそのまま写すため、.fmodelと同じ番号にした。
	 */
	enum class AlphaMode : std::uint32_t
	{
		Opaque = 0, // 不透明
		Mask   = 1, // 透明度が半分より下の所を抜く
		Blend  = 2  // 後ろの色と混ぜる
	};


	/**
	 * @brief メッシュのデータをまとめた構造体
	 * @details 頂点の配列、インデックスの配列、アルファモード、テクスチャのパスなどを持つ。
	 */
	struct MeshData
	{
		std::vector<ModelVertex>  vertices;             // 頂点の配列
		std::vector<std::uint8_t> indices;              // インデックスの配列
		DXGI_FORMAT               indexFormat{};        // インデックスのフォーマット
		UINT                      indexCount{};         // インデックスの数
		AlphaMode                 alphaMode{};          // アルファモード
		float                     baseColorFactor[4]{}; // ベースカラーの係数
		std::filesystem::path     baseColorTexturePath; // ベースカラーのテクスチャのパス（空ならテクスチャなし）
		std::filesystem::path     alphaTexturePath;     // 透明度のテクスチャのパス（空ならテクスチャなし）
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
