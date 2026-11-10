/**
 * @file Model.h
 * @brief モデルを管理するクラスのヘッダ
 */
#pragma once
#include "Core/Noncopyable.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ModelData.h"


namespace fangEngine::render
{
	/**
	 * @brief メッシュ1つぶんのGPUのバッファを持つ構造体
	 */
	struct Mesh
	{
		VertexBuffer vertexBuffer; // 頂点バッファ
		IndexBuffer  indexBuffer;  // インデックスバッファ
		UINT         indexCount{}; // インデックスの数
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
		 * @param device モデルを作るためのID3D12Device
		 * @param modelData モデルのデータ
		 * @return モデルを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(ID3D12Device* device, const ModelData& modelData);

		/**
		 * @brief メッシュの配列を返す
		 * @return メッシュの配列
		 */
		inline const std::vector<Mesh>& GetMeshes() const { return m_meshes; }


	private:
		std::vector<Mesh> m_meshes; // メッシュの配列
	};
} // namespace fangEngine::render
