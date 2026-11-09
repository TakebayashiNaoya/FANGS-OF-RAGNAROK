/**
 * @file RootSignature.cpp
 * @brief ルートシグネチャを管理するクラスの実装
 */
#include "RootSignature.h"
#include "Core/Log.h"


namespace fangEngine::render
{
	bool RootSignature::Initialize(ID3D12Device* device)
	{
		// ルートパラメータを作る
		D3D12_ROOT_PARAMETER rootParameter{};
		rootParameter.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;  // 定数バッファを使う
		rootParameter.Descriptor.ShaderRegister = 0;                              // b0
		rootParameter.Descriptor.RegisterSpace  = 0;                              // レジスタの空間の番号
		rootParameter.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX; // 頂点シェーダーで使う

		// ルートシグネチャの設定を作る
		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSignatureDesc.NumParameters = 1;
		rootSignatureDesc.pParameters   = &rootParameter;

		// バイトの並びに変換する
		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature;
		HRESULT serializeRootSignatureResult = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&serializedRootSignature,
			nullptr
		);
		if (core::LogIfFailed(serializeRootSignatureResult, L"ルートシグネチャの変換に失敗しました。")) return false;

		// ルートシグネチャを作る
		HRESULT createRootSignatureResult = device->CreateRootSignature(
			0,
			serializedRootSignature->GetBufferPointer(),
			serializedRootSignature->GetBufferSize(),
			IID_PPV_ARGS(&m_rootSignature)
		);
		if (core::LogIfFailed(createRootSignatureResult, L"ルートシグネチャの作成に失敗しました。")) return false;

		return true;
	}
} // namespace fangEngine::render
