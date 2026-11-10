/**
 * @file CubePass.h
 * @brief 立方体を描くパス
 */
#pragma once
#include <d3d12.h>
#include "Shader.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ConstantBuffer.h"
#include "Core/Math/Matrix.h"


namespace fangEngine::render
{
	/**
	 * @brief 立方体を描くパス
	 * @details シェーダー、ルートシグネチャ、パイプラインステート、頂点バッファ、インデックスバッファを持ち、立方体を描く。
	 */
	class CubePass
	{
	public:
		/**
		 * @brief 立方体を描くパスを作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool Initialize(ID3D12Device* device);

		/**
		 * @brief 立方体を描く
		 * @param commandList 描画の命令を積むコマンドリスト
		 * @param frameIndex 書き込む定数バッファを決めるフレームの番号
		 * @param world 立方体の置き方を決めるワールド行列
		 * @param viewProjection カメラの見え方を決めるビュー行列とプロジェクション行列の積
		 */
		void Render(ID3D12GraphicsCommandList* commandList, UINT frameIndex, const core::Matrix& world, const core::Matrix& viewProjection);


	private:
		Shader         m_vertexShader;   // 頂点の位置を決めるシェーダー
		Shader         m_pixelShader;    // 三角形が覆うピクセルごとに動き、混ぜた頂点の色で塗るシェーダー
		RootSignature  m_rootSignature;  // シェーダーに渡す値の場所を決めるルートシグネチャ
		PipelineState  m_pipelineState;  // シェーダーやルートシグネチャなどをまとめたパイプラインステート
		VertexBuffer   m_vertexBuffer;   // 頂点の位置と色を入れる頂点バッファ
		IndexBuffer    m_indexBuffer;    // 頂点の順番を決めるインデックスバッファ
		ConstantBuffer m_constantBuffer; // 毎フレームの行列をGPUへ渡す定数バッファ
	};
} // namespace fangEngine::render
