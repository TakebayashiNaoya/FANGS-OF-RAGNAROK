/**
 * @file TrianglePass.h
 * @brief 三角形を描くパス
 */
#pragma once
#include <d3d12.h>
#include "Shader.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "VertexBuffer.h"


namespace fangEngine::render
{
	/**
	 * @brief 三角形を描くパス
	 * @details シェーダー、ルートシグネチャ、パイプラインステート、頂点バッファを持ち、三角形を描く。
	 */
	class TrianglePass
	{
	public:
		/**
		 * @brief 三角形を描くパスを作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool Initialize(ID3D12Device* device);

		/**
		 * @brief 三角形を描く
		 * @param commandList 描画の命令を積むコマンドリスト
		 */
		void Render(ID3D12GraphicsCommandList* commandList);


	private:
		Shader        m_vertexShader;  // 頂点の位置を決めるシェーダー
		Shader        m_pixelShader;   // 三角形が覆うピクセルごとに動き、混ぜた頂点の色で塗るシェーダー
		RootSignature m_rootSignature; // シェーダーに渡す値の場所を決めるルートシグネチャ
		PipelineState m_pipelineState; // シェーダーやルートシグネチャなどをまとめたパイプラインステート
		VertexBuffer  m_vertexBuffer;  // 頂点の位置と色を入れる頂点バッファ
	};
} // namespace fangEngine::render
