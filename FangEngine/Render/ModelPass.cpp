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
		// 頂点の位置、法線、UV、2つ目のUVの読み方を決める
		const D3D12_INPUT_ELEMENT_DESC INPUT_ELEMENT_DESCS[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ModelVertex, normal),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ModelVertex, uv),       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(ModelVertex, secondUv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	}


	bool ModelPass::Initialize(ID3D12Device* device)
	{
		// ベースカラーのテクスチャを使うための設定を作る
		D3D12_DESCRIPTOR_RANGE baseColorTextureDescriptorRange{};
		baseColorTextureDescriptorRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;      // SRVを使う
		baseColorTextureDescriptorRange.NumDescriptors                    = 1;                                    // 1つ使う
		baseColorTextureDescriptorRange.BaseShaderRegister                = 0;                                    // t0
		baseColorTextureDescriptorRange.RegisterSpace                     = 0;                                    // レジスタの空間の番号
		baseColorTextureDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // テーブルの最後に追加

		// アルファのテクスチャを使うための設定を作る
		D3D12_DESCRIPTOR_RANGE alphaTextureDescriptorRange{};
		alphaTextureDescriptorRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;      // SRVを使う
		alphaTextureDescriptorRange.NumDescriptors                    = 1;                                    // 1つ使う
		alphaTextureDescriptorRange.BaseShaderRegister                = 1;                                    // t1
		alphaTextureDescriptorRange.RegisterSpace                     = 0;                                    // レジスタの空間の番号
		alphaTextureDescriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // テーブルの最後に追加

		// ルートパラメータを作る
		D3D12_ROOT_PARAMETER rootParameters[4]{};

		rootParameters[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;  // 定数バッファを使う
		rootParameters[0].Descriptor.ShaderRegister = 0;                              // b0
		rootParameters[0].Descriptor.RegisterSpace  = 0;                              // レジスタの空間の番号
		rootParameters[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX; // 頂点シェーダーで使う

		rootParameters[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; // 32ビットの定数を使う
		rootParameters[1].Constants.ShaderRegister = 1;                                         // b1
		rootParameters[1].Constants.RegisterSpace  = 0;                                         // レジスタの空間の番号
		rootParameters[1].Constants.Num32BitValues = 4;                                         // 4つ使う
		rootParameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;             // ピクセルシェーダーで使う

		rootParameters[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // ディスクリプタテーブルを使う
		rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;                                          // 1つ使う
		rootParameters[2].DescriptorTable.pDescriptorRanges   = &baseColorTextureDescriptorRange;           // ベースカラーのテクスチャを使う
		rootParameters[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;              // ピクセルシェーダーで使う

		rootParameters[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // ディスクリプタテーブルを使う
		rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;                                          // 1つ使う
		rootParameters[3].DescriptorTable.pDescriptorRanges   = &alphaTextureDescriptorRange;               // アルファのテクスチャを使う
		rootParameters[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;              // ピクセルシェーダーで使う

		// サンプラーの設定を作る
		D3D12_STATIC_SAMPLER_DESC staticSamplerDesc{};
		staticSamplerDesc.Filter           = D3D12_FILTER_ANISOTROPIC;        // 斜めから見た面でも、ぼやけないように色を取る（異方性フィルタ）
		staticSamplerDesc.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // U座標の繰り返し
		staticSamplerDesc.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // V座標の繰り返し
		staticSamplerDesc.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // W座標の繰り返し
		staticSamplerDesc.MipLODBias       = 0.0f;                            // ミップの段を計算どおりのまま使う（ずらさない）
		staticSamplerDesc.MaxAnisotropy    = 16;                              // 異方性で色を取る回数を、一番多い16回にする
		staticSamplerDesc.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;     // 比べないため使われないが、0は古いSDKで正しい値にならないため入れる
		staticSamplerDesc.MinLOD           = 0.0f;                            // 一番細かい段から使う
		staticSamplerDesc.MaxLOD           = D3D12_FLOAT32_MAX;               // ミップを全段使う
		staticSamplerDesc.ShaderRegister   = 0;                               // s0
		staticSamplerDesc.RegisterSpace    = 0;                               // レジスタの空間の番号
		staticSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;   // ピクセルシェーダーで使う

		// ルートシグネチャの設定を作る
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSignatureDesc.NumParameters     = _countof(rootParameters);
		rootSignatureDesc.pParameters       = rootParameters;
		rootSignatureDesc.NumStaticSamplers = 1;
		rootSignatureDesc.pStaticSamplers   = &staticSamplerDesc;

		// シェーダー、ルートシグネチャ、定数バッファを作る
		if (m_vertexShader.Load(L"ModelVertexShader.cso") == false)             return false;
		if (m_pixelShader.Load(L"ModelPixelShader.cso") == false)               return false;
		if (m_rootSignature.Initialize(device, rootSignatureDesc) == false)     return false;
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
