/**
 * @file ModelPass.h
 * @brief モデルを描くパス
 */
#pragma once
#include <d3d12.h>
#include "Shader.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "ConstantBuffer.h"
#include "Core/Math/Matrix.h"
#include "Model.h"


namespace fangEngine::render
{
	/**
	 * @brief モデルを描くパス
	 * @details シェーダー、ルートシグネチャ、パイプラインステート、定数バッファを持ち、モデルを描く。
	 */
	class ModelPass
	{
	public:
		/**
		 * @brief モデルを描くパスを作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool Initialize(ID3D12Device* device);

		/**
		 * @brief モデルを描く
		 * @param commandList 描画の命令を積むコマンドリスト
		 * @param frameIndex 書き込む定数バッファを決めるフレームの番号
		 * @param model 描画するモデル
		 * @param world モデルの置き方を決めるワールド行列
		 * @param viewProjection カメラの見え方を決めるビュー行列とプロジェクション行列の積
		 */
		void Render(ID3D12GraphicsCommandList* commandList, UINT frameIndex, const Model& model, const core::Matrix& world, const core::Matrix& viewProjection);


	private:
		Shader         m_vertexShader;   // 頂点の位置を決めるシェーダー
		Shader         m_pixelShader;    // 三角形が覆うピクセルごとに動き、法線の向きを色にして塗るシェーダー
		RootSignature  m_rootSignature;  // シェーダーに渡す値の場所を決めるルートシグネチャ
		PipelineState  m_pipelineState;  // シェーダーやルートシグネチャなどをまとめたパイプラインステート
		ConstantBuffer m_constantBuffer; // 毎フレームの行列をGPUへ渡す定数バッファ
	};
} // namespace fangEngine::render
