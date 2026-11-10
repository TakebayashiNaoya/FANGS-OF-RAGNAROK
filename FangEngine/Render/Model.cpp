/**
 * @file Model.cpp
 * @brief モデルの実装
 */
#include "Model.h"


namespace fangEngine::render
{
	bool Model::Initialize(ID3D12Device* device, const ModelData& modelData)
	{
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
		}

		return true;
	}
} // namespace fangEngine::render
