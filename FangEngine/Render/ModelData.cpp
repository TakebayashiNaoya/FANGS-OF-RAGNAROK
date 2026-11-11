/**
 * @file ModelData.cpp
 * @brief モデルのデータをまとめた構造体を定義する実装
 */
#include "ModelData.h"
#include <fstream>
#include <cstring>
#include "Core/Log.h"


namespace fangEngine::render
{
	namespace
	{
		const std::uint32_t MODEL_VERSION = 2; // .fmodelのバージョン

		// .fmodelのヘッダの構造体
		struct FileHeader
		{
			char magic[4];           // "FMDL"の印
			std::uint32_t version;   // .fmodelのバージョン
			std::uint32_t meshCount; // メッシュの数
		};

		// .fmodelのメッシュのヘッダの構造体
		struct MeshHeader
		{
			std::uint32_t vertexCount; // 頂点の数
			std::uint32_t indexCount;  // インデックスの数
			std::uint32_t indexSize;   // インデックスの1つのバイト数
		};

		// .fmodelのマテリアルのヘッダの構造体
		struct MaterialHeader
		{
			std::uint32_t alphaMode;  // アルファモード
			float baseColorFactor[4]; // ベースカラーの係数
		};

		// 名前を読み、.fmodelのフォルダからのパスにする
		void ReadTexturePath(std::ifstream& file, const std::filesystem::path& folder, std::filesystem::path& texturePath)
		{
			std::uint32_t pathLength{};
			file.read(reinterpret_cast<char*>(&pathLength), sizeof(pathLength));
			if (pathLength == 0) return;

			std::u8string path;
			path.resize(pathLength);
			file.read(reinterpret_cast<char*>(path.data()), path.size());
			texturePath = folder / std::filesystem::path(path);
		}
	}


	bool LoadModel(const std::filesystem::path& path, ModelData& modelData)
	{
		// ファイルのヘッダを読み込む
		FileHeader header{};
		std::ifstream file(path, std::ios::binary);
		if (file.is_open() == false) {
			std::wstring message = L"モデルのファイルを開けませんでした。" + path.wstring();
			core::LogError(message.c_str());
			return false;
		}
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (std::memcmp(header.magic, "FMDL", 4) != 0) { core::LogError(L"モデルの形式ではありません。"); return false; }
		if (header.version != MODEL_VERSION) { core::LogError(L"サポートされていないモデルバージョンです。"); return false; }

		// .fmodelのフォルダを取得する
		const std::filesystem::path modelFolder = path.parent_path();

		// メッシュの数だけデータを読み込む
		modelData.meshes.resize(header.meshCount);
		for (MeshData& mesh : modelData.meshes)
		{
			// メッシュのヘッダを読み込む
			MeshHeader meshHeader{};
			file.read(reinterpret_cast<char*>(&meshHeader), sizeof(meshHeader));

			// マテリアルのヘッダを読み込む
			MaterialHeader materialHeader{};
			file.read(reinterpret_cast<char*>(&materialHeader), sizeof(materialHeader));

			// マテリアルのデータを設定する
			mesh.alphaMode = static_cast<AlphaMode>(materialHeader.alphaMode);
			mesh.baseColorFactor[0] = materialHeader.baseColorFactor[0];
			mesh.baseColorFactor[1] = materialHeader.baseColorFactor[1];
			mesh.baseColorFactor[2] = materialHeader.baseColorFactor[2];
			mesh.baseColorFactor[3] = materialHeader.baseColorFactor[3];

			// ベースカラーのテクスチャのパスを読み込む
			ReadTexturePath(file, modelFolder, mesh.baseColorTexturePath);
			// 透明度のテクスチャのパスを読み込む
			ReadTexturePath(file, modelFolder, mesh.alphaTexturePath);

			// メッシュのインデックス数を設定する
			mesh.indexCount = meshHeader.indexCount;

			// メッシュのインデックスフォーマットを設定する
			// インデックスのバイト数が2なら16ビット、4なら32ビット
			mesh.indexFormat = (meshHeader.indexSize == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

			// メッシュの頂点とインデックスの配列を確保する
			mesh.vertices.resize(meshHeader.vertexCount);
			mesh.indices.resize(meshHeader.indexCount * meshHeader.indexSize);

			// メッシュの頂点とインデックスの配列を読み込む
			file.read(reinterpret_cast<char*>(mesh.vertices.data()), mesh.vertices.size() * sizeof(ModelVertex));
			file.read(reinterpret_cast<char*>(mesh.indices.data()), mesh.indices.size());
		}
		// read()で頼まれたバイト数を読み切れなかったときに印(failbit)が立ち、fail()がtrueを返す。
		if (file.fail()) { core::LogError(L"モデルのファイルが途中で終わっています。"); return false; }

		return true;
	}
} // namespace fangEngine::render
