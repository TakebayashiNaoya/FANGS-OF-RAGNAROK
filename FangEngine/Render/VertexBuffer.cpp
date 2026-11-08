/**
 * @file VertexBuffer.cpp
 * @brief 頂点バッファを管理するクラスの実装
 */
#include "VertexBuffer.h"
#include <cstring>
#include "Core/Log.h"


namespace fangEngine::render
{
	bool VertexBuffer::Initialize(ID3D12Device* device, const void* vertices, UINT size, UINT stride)
	{
		// 頂点バッファを作る
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width            = size;
		resourceDesc.Height           = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels        = 1;
		resourceDesc.Format           = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT createCommittedResourceResult = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_resource)
		);
		if (core::LogIfFailed(createCommittedResourceResult, L"頂点バッファの作成に失敗しました。")) return false;

		// 頂点バッファに頂点のデータを写す
		void* mappedVertices = nullptr;
		D3D12_RANGE readRange{};
		HRESULT mapResult = m_resource->Map(0, &readRange, &mappedVertices);
		if (core::LogIfFailed(mapResult, L"頂点バッファのマップに失敗しました。")) return false;
		std::memcpy(mappedVertices, vertices, size);
		m_resource->Unmap(0, nullptr);

		// 頂点バッファのビューに読み方を書く
		m_view.BufferLocation = m_resource->GetGPUVirtualAddress(); // GPUから見たバッファの場所
		m_view.SizeInBytes    = size;
		m_view.StrideInBytes  = stride;

		return true;
	}
} // namespace fangEngine::render
