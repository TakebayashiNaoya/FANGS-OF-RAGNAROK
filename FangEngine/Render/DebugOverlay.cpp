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

		// ディスクリプタの増分サイズを取得
		m_descriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// 空いているディスクリプタの番号を初期化
		m_freeDescriptorIndices.reserve(SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT);
		for (UINT descriptorIndex = 0; descriptorIndex < SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT; descriptorIndex++) {
			m_freeDescriptorIndices.push_back(descriptorIndex);
		}

		return true;
	}


	void DebugOverlay::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
	{
		// 空いている番号を1つ取り出す
		UINT descriptorIndex = m_freeDescriptorIndices.back();
		m_freeDescriptorIndices.pop_back();
		// 番号をCPUとGPUのアドレスに変える
		cpuHandle->ptr = m_shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + descriptorIndex * m_descriptorHandleIncrementSize;
		gpuHandle->ptr = m_shaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + descriptorIndex * m_descriptorHandleIncrementSize;
	}


	void DebugOverlay::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
	{
		// アドレスを番号に戻して一覧へ返す(GPUでも同じ結果(番号)になるためCPUだけでOK)
		UINT descriptorIndex = static_cast<UINT>((cpuHandle.ptr - m_shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr) / m_descriptorHandleIncrementSize);
		m_freeDescriptorIndices.push_back(descriptorIndex);
	}
} // namespace fangEngine::render
