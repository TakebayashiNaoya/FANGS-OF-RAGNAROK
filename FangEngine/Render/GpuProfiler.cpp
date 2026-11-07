/**
 * @file GpuProfiler.cpp
 * @brief GPUの処理時間を計測するクラスの実装
 */
#include "GpuProfiler.h"
#include <iostream>
#include "RenderContext.h"


namespace fangEngine::render
{
	namespace
	{
		constexpr UINT TIMESTAMP_COUNT_PER_FRAME = 2; // 先頭と末尾
		constexpr UINT TIMESTAMP_COUNT           = TIMESTAMP_COUNT_PER_FRAME * RenderContext::RENDER_TARGET_COUNT;
	}


	bool GpuProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
	{
		if (CreateQueryHeap(device) == false)      return false;
		if (CreateReadbackBuffer(device) == false) return false;

		HRESULT result = commandQueue->GetTimestampFrequency(&m_timestampFrequency);
		if (FAILED(result)) {
			std::cerr << "GPUの時計の速さの取得に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}


	bool GpuProfiler::CreateQueryHeap(ID3D12Device* device)
	{
		D3D12_QUERY_HEAP_DESC queryHeapDesc{};
		queryHeapDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		queryHeapDesc.Count = TIMESTAMP_COUNT;

		HRESULT result = device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap));
		if (FAILED(result)) {
			std::cerr << "クエリヒープの作成に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}


	bool GpuProfiler::CreateReadbackBuffer(ID3D12Device* device)
	{
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_READBACK;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width            = sizeof(UINT64) * TIMESTAMP_COUNT;
		resourceDesc.Height           = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels        = 1;
		resourceDesc.Format           = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		HRESULT result = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_readbackBuffer)
		);
		if (FAILED(result)) {
			std::cerr << "読み出し用バッファの作成に失敗しました。" << std::endl;
			return false;
		}

		return true;
	}
} // namespace fangEngine::render
