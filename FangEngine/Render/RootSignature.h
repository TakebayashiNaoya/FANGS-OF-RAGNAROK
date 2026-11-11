/**
 * @file RootSignature.h
 * @brief ルートシグネチャを管理するクラス
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"


namespace fangEngine::render
{
	/**
	 * @brief ルートシグネチャを管理するクラス
	 * @details GPUのオブジェクトを持つためコピー禁止。
	 */
	class RootSignature : private core::Noncopyable
	{
	public:
		/**
		 * @brief ルートシグネチャを作る
		 * @param device ルートシグネチャを作るためのID3D12Device
		 * @param rootSignatureDesc ルートシグネチャの設定
		 * @return ルートシグネチャを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc);

		/**
		 * @brief ルートシグネチャを返す
		 * @return ルートシグネチャ
		 */
		inline ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }


	private:
		Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
	};
} // namespace fangEngine::render
