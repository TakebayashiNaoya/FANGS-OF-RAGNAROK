/**
 * @file GpuProfiler.cpp
 * @brief GPUの処理時間を計測するクラスの実装
 */
#include "GpuProfiler.h"
#include <iostream>
#include "RenderContext.h"


namespace fangEngine::render
{
#ifdef FANG_DEBUG_OR_PROFILE

	namespace
	{
		constexpr UINT TIMESTAMP_COUNT_PER_FRAME = 2; // 先頭と末尾
		constexpr UINT TIMESTAMP_COUNT           = TIMESTAMP_COUNT_PER_FRAME * RenderContext::RENDER_TARGET_COUNT;

		constexpr float MILLISECONDS_PER_SECOND = 1000.0f;
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


	void GpuProfiler::BeginFrame(ID3D12GraphicsCommandList* commandList, UINT frameIndex)
	{
		ReadFrameMilliseconds(frameIndex);

		// 測る処理の先頭でタイムスタンプを取る
		commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frameIndex * TIMESTAMP_COUNT_PER_FRAME);
	}


	void GpuProfiler::EndFrame(ID3D12GraphicsCommandList* commandList, UINT frameIndex)
	{
		UINT firstTimestampIndex = frameIndex * TIMESTAMP_COUNT_PER_FRAME;

		// 測る処理の末尾でタイムスタンプを取る
		commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, firstTimestampIndex + 1);
		// 読み戻しのバッファへ写す
		commandList->ResolveQueryData(
			m_queryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			firstTimestampIndex,
			TIMESTAMP_COUNT_PER_FRAME,
			m_readbackBuffer.Get(),
			sizeof(UINT64) * firstTimestampIndex
		);
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


	void GpuProfiler::ReadFrameMilliseconds(UINT frameIndex)
	{
		UINT firstTimestampIndex = frameIndex * TIMESTAMP_COUNT_PER_FRAME;

		D3D12_RANGE readRange{};
		readRange.Begin = sizeof(UINT64) * firstTimestampIndex;
		readRange.End   = readRange.Begin + sizeof(UINT64) * TIMESTAMP_COUNT_PER_FRAME;

		void* mappedData = nullptr;
		HRESULT result = m_readbackBuffer->Map(0, &readRange, &mappedData);
		if (FAILED(result)) {
			std::cerr << "読み戻しのバッファを開けませんでした。" << std::endl;
			return;
		}

		const UINT64* timestamps = static_cast<const UINT64*>(mappedData);
		UINT64 timestampDifference = timestamps[firstTimestampIndex + 1] - timestamps[firstTimestampIndex];
		double elapsedSeconds = static_cast<double>(timestampDifference) / static_cast<double>(m_timestampFrequency);
		m_frameMilliseconds = static_cast<float>(elapsedSeconds * MILLISECONDS_PER_SECOND); // 秒をミリ秒に変換

		D3D12_RANGE writtenRange{}; // CPUは書いていないため、範囲は空にする
		m_readbackBuffer->Unmap(0, &writtenRange);
	}

#else

	// Releaseでは計測しないため、何もしない関数を用意する
	bool GpuProfiler::Initialize(ID3D12Device*, ID3D12CommandQueue*) { return true; }
	void GpuProfiler::BeginFrame(ID3D12GraphicsCommandList*, UINT) {}
	void GpuProfiler::EndFrame(ID3D12GraphicsCommandList*, UINT) {}

#endif // FANG_DEBUG_OR_PROFILE
} // namespace fangEngine::render
