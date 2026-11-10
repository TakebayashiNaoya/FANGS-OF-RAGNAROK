/**
 * @file ModelPass.cpp
 * @brief モデルを描くパス
 */
#include "ModelPass.h"
#include <cstddef>
#include <cstdint>
#include "RenderContext.h"


namespace fangEngine::render
{
	namespace
	{
		// 頂点の位置、法線、UVの読み方を決める
		const D3D12_INPUT_ELEMENT_DESC INPUT_ELEMENT_DESCS[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelVertex, normal),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ModelVertex, uv),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	}


	bool ModelPass::Initialize(ID3D12Device* device)
	{
		// シェーダー、ルートシグネチャ、定数バッファを作る
		if (m_vertexShader.Load(L"ModelVertexShader.cso") == false)             return false;
		if (m_pixelShader.Load(L"ModelPixelShader.cso") == false)               return false;
		if (m_rootSignature.Initialize(device) == false)                        return false;
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


	void ModelPass::Render(ID3D12GraphicsCommandList* commandList, UINT frameIndex, const Model& model, const core::Matrix& world, const core::Matrix& viewProjection)
	{
		// モデルの置き方とカメラの見え方を決める行列を掛け合わせる
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

		// メッシュごとにバッファをセットして描く
		for (const Mesh& mesh : model.GetMeshes())
		{
			// 頂点バッファをセットする
			commandList->IASetVertexBuffers(0, 1, &mesh.vertexBuffer.GetView());
			// インデックスバッファをセットする
			commandList->IASetIndexBuffer(&mesh.indexBuffer.GetView());
			// メッシュを描く
			commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
		}
	}
} // namespace fangEngine::render
