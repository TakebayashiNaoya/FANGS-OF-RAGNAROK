/**
 * @file Model.h
 * @brief モデルを管理するクラスのヘッダ
 */
#pragma once
#include <map>
#include "Core/Noncopyable.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ModelData.h"
#include "Texture.h"


namespace fangEngine::render
{
	/**
	 * @brief メッシュ1つ分描くのに要るものを持つ構造体
	 */
	struct Mesh
	{
		VertexBuffer   vertexBuffer;         // 頂点バッファ
		IndexBuffer    indexBuffer;          // インデックスバッファ
		UINT           indexCount{};         // インデックスの数
		AlphaMode      alphaMode{};          // アルファモード
		float          baseColorFactor[4]{}; // ベースカラーの係数
		const Texture* baseColorTexture{};   // ベースカラーのテクスチャ（テクスチャのないメッシュは1×1の白）
		const Texture* alphaTexture{};       // 透明度のテクスチャ（テクスチャのないメッシュは1×1の白）
	};


	/**
	 * @brief モデルを管理するクラス
	 * @details GPUのオブジェクトを持つためコピー禁止。
	 */
	class Model : private core::Noncopyable
	{
	public:
		/**
		 * @brief モデルを作る
		 * @param renderContext モデルを作るためのRenderContext
		 * @param modelData モデルのデータ
		 * @return モデルを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(RenderContext& renderContext, const ModelData& modelData);

		/**
		 * @brief メッシュの配列を返す
		 * @return メッシュの配列
		 */
		inline const std::vector<Mesh>& GetMeshes() const { return m_meshes; }


	private:
		/**
		 * @brief テクスチャを作るか、すでに作ったテクスチャを返す
		 * @param renderContext テクスチャを作るためのRenderContext
		 * @param path テクスチャのパス（空なら1×1の白）
		 * @return 読めたら作ったテクスチャ、読めなかったらnullptr
		 */
		const Texture* GetOrCreateTexture(RenderContext& renderContext, const std::filesystem::path& path);


	private:
		// 同じパスは1枚だけ作る。
		// m_meshesがm_texturesを指しているため、m_texturesを後に消す必要がある。
		// メンバ変数は下から消えるため、m_meshesの上に宣言する。
		std::map<std::filesystem::path, Texture> m_textures;

		std::vector<Mesh> m_meshes; // メッシュの配列
	};
} // namespace fangEngine::render
