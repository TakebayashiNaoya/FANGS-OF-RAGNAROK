/**
 * @file CubePass.cpp
 * @brief 立方体を描くパス
 */
#include "CubePass.h"
#include <cstddef>
#include <cstdint>
#include "RenderContext.h"


namespace fangEngine::render
{
	namespace
	{
		// 立方体の頂点の位置と色の構造体
		struct Vertex
		{
			float position[3]; // 頂点の位置
			float color[4];    // 頂点の色
		};

		// 立方体の頂点の位置と色を入れる配列
		const Vertex VERTICES[] =
		{
			{ { -0.5f, 0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f, 1.0f } },  // 手前の左上(赤)
			{ { 0.5f, 0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f, 1.0f } },   // 手前の右上(緑)
			{ { 0.5f, -0.5f, -0.5f },{ 0.0f, 0.0f, 1.0f, 1.0f } },  // 手前の右下(青)
			{ { -0.5f, -0.5f, -0.5f },{ 1.0f, 1.0f, 0.0f, 1.0f } }, // 手前の左下(黄)
			{ { -0.5f, 0.5f, 0.5f },{ 1.0f, 0.0f, 1.0f, 1.0f } },   // 奥の左上(紫)
			{ { 0.5f, 0.5f, 0.5f },{ 0.0f, 1.0f, 1.0f, 1.0f } },    // 奥の右上(水色)
			{ { 0.5f, -0.5f, 0.5f },{ 1.0f, 1.0f, 1.0f, 1.0f } },   // 奥の右下(白)
			{ { -0.5f, -0.5f, 0.5f },{ 0.0f, 0.0f, 0.0f, 1.0f } }   // 奥の左下(黒)
		};

		// インデックスの配列
		const std::uint16_t INDICES[] =
		{
			0, 1, 2, 0, 2, 3, // 手前の面
			5, 4, 7, 5, 7, 6, // 奥の面
			0, 3, 7, 0, 7, 4, // 左の面
			1, 5, 6, 1, 6, 2, // 右の面
			4, 5, 1, 4, 1, 0, // 上の面
			3, 2, 6, 3, 6, 7  // 下の面
		};

		// 頂点の位置と色の読み方を決める
		const D3D12_INPUT_ELEMENT_DESC INPUT_ELEMENT_DESCS[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	}


	bool CubePass::Initialize(ID3D12Device* device)
	{
		// シェーダー、ルートシグネチャ、頂点バッファ、定数バッファを作る
		if (m_vertexShader.Load(L"CubeVertexShader.cso") == false)                                     return false;
		if (m_pixelShader.Load(L"CubePixelShader.cso") == false)                                       return false;
		if (m_rootSignature.Initialize(device) == false)                                               return false;
		if (m_vertexBuffer.Initialize(device, VERTICES, sizeof(VERTICES), sizeof(Vertex)) == false)    return false;
		if (m_indexBuffer.Initialize(device, INDICES, sizeof(INDICES), DXGI_FORMAT_R16_UINT) == false) return false;
		if (m_constantBuffer.Initialize(device, sizeof(core::Matrix)) == false)                        return false;

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
		pipelineStateDesc.DepthStencilState.DepthEnable                    = TRUE;                                   // 深度で前後を比べる
		pipelineStateDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;             // 深度バッファに書き込む
		pipelineStateDesc.DepthStencilState.DepthFunc                      = D3D12_COMPARISON_FUNC_LESS;             // 今より手前なら描く
		pipelineStateDesc.InputLayout                                      = { INPUT_ELEMENT_DESCS, _countof(INPUT_ELEMENT_DESCS) };
		pipelineStateDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // 三角形を描く
		pipelineStateDesc.NumRenderTargets                                 = 1;                                      // 描き先は1つ
		pipelineStateDesc.RTVFormats[0]                                    = RenderContext::RENDER_TARGET_FORMAT;    // 描き先と同じ形式
		pipelineStateDesc.DSVFormat                                        = RenderContext::DEPTH_STENCIL_FORMAT;    // 深度バッファと同じ形式
		pipelineStateDesc.SampleDesc.Count                                 = 1;                                      // マルチサンプリングしない
		if (m_pipelineState.Initialize(device, pipelineStateDesc) == false) return false;

		return true;
	}



	void CubePass::Render(ID3D12GraphicsCommandList* commandList, UINT frameIndex, const core::Matrix& world, const core::Matrix& viewProjection)
	{
		// 立方体の置き方とカメラの見え方を決める行列を掛け合わせる
		const core::Matrix worldViewProjection = world * viewProjection;
		// 定数バッファに掛け合わせた行列を書き込む
		m_constantBuffer.Copy(frameIndex, &worldViewProjection);
		// ルートシグネチャをセットする
		commandList->SetGraphicsRootSignature(m_rootSignature.GetRootSignature());
		// 定数バッファの番地をセットする
		commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer.GetGpuVirtualAddress(frameIndex));
		// パイプラインステートをセットする
		commandList->SetPipelineState(m_pipelineState.GetPipelineState());
		// インデックス3つにつき三角形1つを描くことを伝える
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// 頂点バッファをセットする
		commandList->IASetVertexBuffers(0, 1, &m_vertexBuffer.GetView());
		// インデックスバッファをセットする
		commandList->IASetIndexBuffer(&m_indexBuffer.GetView());
		// 立方体を描く
		commandList->DrawIndexedInstanced(_countof(INDICES), 1, 0, 0, 0);
	}
} // namespace fangEngine::render
