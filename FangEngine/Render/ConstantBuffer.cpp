/**
 * @file ConstantBuffer.cpp
 * @brief 定数バッファを管理するクラスの実装
 */
#include "ConstantBuffer.h"
#include <cstring>
#include "Core/Log.h"


namespace fangEngine::render
{
	bool ConstantBuffer::Initialize(ID3D12Device* device, UINT size)
	{
		// 定数バッファのサイズは256バイト単位で作られるため、呼ぶ側が頼んだサイズを256バイト単位に切り上げる
		UINT alignedSize = (size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) / D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

		// 定数バッファの設定を作る
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width            = alignedSize;
		resourceDesc.Height           = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels        = 1;
		resourceDesc.Format           = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		// 定数バッファをRENDER_TARGET_COUNT個作る
		for (int bufferIndex = 0; bufferIndex < RenderContext::RENDER_TARGET_COUNT; ++bufferIndex)
		{
			HRESULT createCommittedResourceResult = device->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&resourceDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_resources[bufferIndex])
			);
			if (core::LogIfFailed(createCommittedResourceResult, L"定数バッファの作成に失敗しました。")) return false;

			// Mapしたまま持ち、CPUから書ける番地を覚える
			D3D12_RANGE readRange{};
			HRESULT mapResult = m_resources[bufferIndex]->Map(0, &readRange, &m_mappedData[bufferIndex]);
			if (core::LogIfFailed(mapResult, L"定数バッファのマップに失敗しました。")) return false;
		}

		// Copyで写すバイト数に使うため、頼まれた大きさを覚える
		m_size = size;

		return true;
	}


	void ConstantBuffer::Copy(UINT frameIndex, const void* data)
	{
		// CPU側で作ったデータを、GPU側の定数バッファに写す
		std::memcpy(m_mappedData[frameIndex], data, m_size);
	}


	D3D12_GPU_VIRTUAL_ADDRESS ConstantBuffer::GetGpuVirtualAddress(UINT frameIndex) const
	{
		return m_resources[frameIndex]->GetGPUVirtualAddress();
	}
} // namespace fangEngine::render
