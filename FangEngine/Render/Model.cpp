/**
 * @file Model.cpp
 * @brief モデルの実装
 */
#include "Model.h"
#include "RenderContext.h"


namespace fangEngine::render
{
	bool Model::Initialize(RenderContext& renderContext, const ModelData& modelData)
	{
		ID3D12Device* device = renderContext.GetDevice();

		// resizeは配列を広げるときに中身を新しい場所へ移す処理を含むが、
		// Meshはコピー禁止のVertexBufferを持ち、移せずにコンパイルが通らないため、
		// サイズを指定して配列を作る。
		m_meshes = std::vector<Mesh>(modelData.meshes.size());

		// CPU側のモデルデータをGPU側のバッファに写す
		for (size_t i = 0; i < modelData.meshes.size(); ++i)
		{
			// メッシュのデータを取得
			const MeshData& meshData = modelData.meshes[i];
			Mesh& mesh = m_meshes[i];

			// 頂点バッファを作る
			bool vbResult = mesh.vertexBuffer.Initialize(
				device,
				meshData.vertices.data(),
				static_cast<UINT>(meshData.vertices.size() * sizeof(ModelVertex)),
				sizeof(ModelVertex)
			);
			if (vbResult == false) return false;

			// インデックスバッファを作る
			bool ibResult = mesh.indexBuffer.Initialize(
				device,
				meshData.indices.data(),
				static_cast<UINT>(meshData.indices.size()),
				meshData.indexFormat
			);
			if (ibResult == false) return false;

			// インデックスの数を保存
			mesh.indexCount = meshData.indexCount;

			// アルファモードとベースカラーの係数を保存
			mesh.alphaMode = meshData.alphaMode;
			mesh.baseColorFactor[0] = meshData.baseColorFactor[0];
			mesh.baseColorFactor[1] = meshData.baseColorFactor[1];
			mesh.baseColorFactor[2] = meshData.baseColorFactor[2];
			mesh.baseColorFactor[3] = meshData.baseColorFactor[3];

			// ベースカラーのテクスチャを作るか、すでに作ったテクスチャを返す
			mesh.baseColorTexture = GetOrCreateTexture(renderContext, meshData.baseColorTexturePath);
			if (mesh.baseColorTexture == nullptr) return false;

			// 透明度のテクスチャを作るか、すでに作ったテクスチャを返す
			mesh.alphaTexture = GetOrCreateTexture(renderContext, meshData.alphaTexturePath);
			if (mesh.alphaTexture == nullptr) return false;
		}

		return true;
	}


	const Texture* Model::GetOrCreateTexture(RenderContext& renderContext, const std::filesystem::path& path)
	{
		// 返り値は「要素を指すイテレータ」と「今作ったか（bool）」の組
		const auto [textureIterator, created] = m_textures.try_emplace(path);

		// 今作っていなければ（すでにあれば）そのまま返す
		if (created == false) return &textureIterator->second;

		// 今作った場合、テクスチャを作る
		TextureData textureData{};
		// パスが空なら白を作る
		if (path.empty())
		{
			textureData.format   = DXGI_FORMAT_R8G8B8A8_UNORM;
			textureData.width    = 1;
			textureData.height   = 1;
			textureData.mipCount = 1;
			textureData.pixels   = { 255, 255, 255, 255 }; // 白
		}
		else
		{
			if (LoadTexture(path, textureData) == false) {
				// 読めなかった場合、作った空のTextureを消す
				m_textures.erase(textureIterator);
				return nullptr;
			}
		}

		// GPU側のテクスチャを作る
		if (textureIterator->second.Initialize(renderContext, textureData) == false) {
			// 作れなかった場合、作った空のTextureを消す
			m_textures.erase(textureIterator);
			return nullptr;
		}

		return &textureIterator->second;
	}
} // namespace fangEngine::render
