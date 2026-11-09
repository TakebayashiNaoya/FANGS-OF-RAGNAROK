/**
 * @file TrianglePass.cpp
 * @brief 三角形を描くパス
 */
#include "TrianglePass.h"
#include <cstddef>
#include "RenderContext.h"


namespace fangEngine::render
{
	namespace
	{
		// 三角形の頂点の位置と色の構造体
		struct Vertex
		{
			float position[3]; // 頂点の位置
			float color[4];    // 頂点の色
		};

		// 三角形の頂点の位置と色を入れる配列
		const Vertex VERTICES[] =
		{
			{ { 0.0f,  0.5f, 0.0f },{ 1.0f, 0.0f, 0.0f, 1.0f } }, // 上の頂点(赤)
			{ { 0.5f, -0.5f, 0.0f },{ 0.0f, 1.0f, 0.0f, 1.0f } }, // 右下の頂点(緑)
			{ { -0.5f, -0.5f, 0.0f },{ 0.0f, 0.0f, 1.0f, 1.0f } }  // 左下の頂点(青)
		};

		// 頂点の位置と色の読み方を決める
		const D3D12_INPUT_ELEMENT_DESC INPUT_ELEMENT_DESCS[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	}


	bool TrianglePass::Initialize(ID3D12Device* device)
	{
		// シェーダー、ルートシグネチャ、頂点バッファ、定数バッファを作る
		if (m_vertexShader.Load(L"TriangleVertexShader.cso") == false) return false;
		if (m_pixelShader.Load(L"TrianglePixelShader.cso") == false)   return false;
		if (m_rootSignature.Initialize(device) == false)               return false;
		if (m_vertexBuffer.Initialize(device, VERTICES, sizeof(VERTICES), sizeof(Vertex)) == false) return false;
		if (m_constantBuffer.Initialize(device, sizeof(core::Matrix)) == false) return false;

		// パイプラインステートを作る
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
		pipelineStateDesc.pRootSignature                                   = m_rootSignature.GetRootSignature();
		pipelineStateDesc.VS                                               = m_vertexShader.GetByteCode();
		pipelineStateDesc.PS                                               = m_pixelShader.GetByteCode();
		pipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;           // RGBA全部書き込む
		pipelineStateDesc.SampleMask                                       = UINT_MAX;                               // 全てのピクセルを描く
		pipelineStateDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;                  // 中を塗る
		pipelineStateDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_BACK;                   // 裏面は描かない
		pipelineStateDesc.RasterizerState.DepthClipEnable                  = TRUE;                                   // クリッピングする
		pipelineStateDesc.InputLayout                                      = { INPUT_ELEMENT_DESCS, _countof(INPUT_ELEMENT_DESCS) };
		pipelineStateDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // 三角形を描く
		pipelineStateDesc.NumRenderTargets                                 = 1;                                      // 描き先は1つ
		pipelineStateDesc.RTVFormats[0]                                    = RenderContext::RENDER_TARGET_FORMAT;    // 描き先と同じ形式
		pipelineStateDesc.SampleDesc.Count                                 = 1;                                      // マルチサンプリングしない
		if (m_pipelineState.Initialize(device, pipelineStateDesc) == false) return false;

		return true;
	}



	void TrianglePass::Render(ID3D12GraphicsCommandList* commandList, UINT frameIndex, const core::Matrix& world)
	{
		// 定数バッファに三角形の置き方を決める行列を書き込む
		m_constantBuffer.Copy(frameIndex, &world);
		// ルートシグネチャをセットする
		commandList->SetGraphicsRootSignature(m_rootSignature.GetRootSignature());
		// 定数バッファの番地をセットする
		commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer.GetGpuVirtualAddress(frameIndex));
		// パイプラインステートをセットする
		commandList->SetPipelineState(m_pipelineState.GetPipelineState());
		// 頂点3つにつき三角形1つを描くことを伝える(1つの頂点を複数の三角形で使用しない)
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// 頂点バッファをセットする
		commandList->IASetVertexBuffers(0, 1, &m_vertexBuffer.GetView());
		// 三角形を描く
		commandList->DrawInstanced(_countof(VERTICES), 1, 0, 0);
	}
} // namespace fangEngine::render
