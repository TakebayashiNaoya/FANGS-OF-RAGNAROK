/**
 * @file DebugOverlay.cpp
 * @brief デバッグ用オーバーレイ描画クラスの実装
 */
#include "DebugOverlay.h"
#include <iostream>
#include "RenderContext.h"


namespace fangEngine::render
{
	namespace
	{
		constexpr UINT SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT = 64;
	}


	bool DebugOverlay::Initialize(HWND window, RenderContext& renderContext)
	{
		return CreateShaderResourceViewDescriptorHeap(renderContext.GetDevice());
	}


	bool DebugOverlay::CreateShaderResourceViewDescriptorHeap(ID3D12Device* device)
	{
		// シェーダーリソースビュー用のディスクリプタヒープの作成
		D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewDescriptorHeapDescription{};
		shaderResourceViewDescriptorHeapDescription.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		shaderResourceViewDescriptorHeapDescription.NumDescriptors = SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT;
		shaderResourceViewDescriptorHeapDescription.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		HRESULT createDescriptorHeapResult = device->CreateDescriptorHeap(
			&shaderResourceViewDescriptorHeapDescription,
			IID_PPV_ARGS(&m_shaderResourceViewDescriptorHeap)
		);
		if (FAILED(createDescriptorHeapResult)) {
			std::cerr << "シェーダーリソースビュー用のディスクリプタヒープの作成に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}
} // namespace fangEngine::render
