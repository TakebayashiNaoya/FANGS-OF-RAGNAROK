/**
 * @file RootSignature.cpp
 * @brief ルートシグネチャを管理するクラスの実装
 */
#include "RootSignature.h"
#include "Core/Log.h"


namespace fangEngine::render
{
	bool RootSignature::Initialize(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc)
	{
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
