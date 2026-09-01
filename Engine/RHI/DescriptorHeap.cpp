/**
 * @file DescriptorHeap.cpp
 * @brief シェーダから見えるディスクリプタヒープの実装。
 */
#include "Pch.h"
#include "RHI/DescriptorHeap.h"


namespace fang::rhi
{
	namespace
	{
		/** @brief シェーダから見えるディスクリプタヒープの大きさ。Phase 3 でリングバッファにする。 */
		constexpr uint32_t SHADER_VISIBLE_DESCRIPTOR_COUNT = 64;
	} // namespace

	bool DescriptorHeap::Initialize(ID3D12Device& device)
	{
		D3D12_DESCRIPTOR_HEAP_DESC shaderVisibleHeapDesc{};
		shaderVisibleHeapDesc.NumDescriptors = SHADER_VISIBLE_DESCRIPTOR_COUNT;
		shaderVisibleHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		shaderVisibleHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (!CheckHresult(
				device.CreateDescriptorHeap(&shaderVisibleHeapDesc, IID_PPV_ARGS(&m_heap)),
				"シェーダ可視ディスクリプタヒープの生成"
			))
		{
			return false;
		}

		m_descriptorSize = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		return true;
	}

	void DescriptorHeap::Shutdown()
	{
		m_heap.Reset();
	}

	bool DescriptorHeap::Allocate(uint32_t& outIndex)
	{
		if (m_nextDescriptor >= SHADER_VISIBLE_DESCRIPTOR_COUNT)
		{
			FANG_LOG_ERROR(RHI, "シェーダ可視ディスクリプタが足りない");
			return false;
		}

		outIndex = m_nextDescriptor;
		++m_nextDescriptor;

		return true;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetCPUHandle(uint32_t index) const
	{
		D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;
		return handle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetGPUHandle(uint32_t index) const
	{
		D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += static_cast<UINT64>(index) * m_descriptorSize;
		return handle;
	}
} // namespace fang::rhi
