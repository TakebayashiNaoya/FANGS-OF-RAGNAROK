/**
 * @file TexturePool.cpp
 * @brief テクスチャの台帳の実装。
 */
#include "Pch.h"
#include "RHI/TexturePool.h"
#include "Core/Log/Assert.h"
#include <cstring>


namespace fang::rhi
{
	TextureHandle TexturePool::Create(
		ID3D12Device&       device,
		ID3D12CommandQueue& commandQueue,
		GPUFence&           fence,
		DescriptorHeap&     descriptorHeap,
		const void*         pixels,
		uint32_t            width,
		uint32_t            height
	)
	{
		uint32_t descriptorIndex = 0;
		if (!descriptorHeap.Allocate(descriptorIndex))
		{
			return TextureHandle{};
		}

		D3D12_HEAP_PROPERTIES defaultHeapProperties{};
		defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width            = width;
		textureDesc.Height           = height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels        = 1;
		textureDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		Entry entry;
		if (!CheckHresult(
				device.CreateCommittedResource(
					&defaultHeapProperties,
					D3D12_HEAP_FLAG_NONE,
					&textureDesc,
					D3D12_RESOURCE_STATE_COPY_DEST,
					nullptr,
					IID_PPV_ARGS(&entry.resource)
				),
				"テクスチャの生成"
			))
		{
			return TextureHandle{};
		}

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT64                             uploadSize = 0;
		device.GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

		ComPtr<ID3D12Resource> uploadBuffer;
		if (!CreateUploadBuffer(&device, static_cast<uint32_t>(uploadSize), uploadBuffer))
		{
			return TextureHandle{};
		}

		uint8_t*    mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		if (!CheckHresult(
				uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
				"テクスチャ転送用の Map"
			))
		{
			return TextureHandle{};
		}

		// 行ごとのピッチが 256 バイト境界に合わされるので 1 行ずつ詰める。
		const uint8_t* source = static_cast<const uint8_t*>(pixels);
		for (uint32_t row = 0; row < height; ++row)
		{
			std::memcpy(
				mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
				source + static_cast<size_t>(row) * width * 4,
				static_cast<size_t>(width) * 4
			);
		}

		uploadBuffer->Unmap(0, nullptr);

		// 転送はフレームの外で済ませたいので、その場で 1 本流して待つ。
		ComPtr<ID3D12CommandAllocator>    uploadAllocator;
		ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
		if (!CheckHresult(
				device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator)),
				"転送用コマンドアロケータの生成"
			))
		{
			return TextureHandle{};
		}

		if (!CheckHresult(
				device.CreateCommandList(
					0,
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					uploadAllocator.Get(),
					nullptr,
					IID_PPV_ARGS(&uploadCommandList)
				),
				"転送用コマンドリストの生成"
			))
		{
			return TextureHandle{};
		}

		D3D12_TEXTURE_COPY_LOCATION copySource{};
		copySource.pResource       = uploadBuffer.Get();
		copySource.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		copySource.PlacedFootprint = footprint;

		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource        = entry.resource.Get();
		destination.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = 0;

		uploadCommandList->CopyTextureRegion(&destination, 0, 0, 0, &copySource, nullptr);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = entry.resource.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		uploadCommandList->ResourceBarrier(1, &barrier);

		FANG_VERIFY(SUCCEEDED(uploadCommandList->Close()));

		ID3D12CommandList* commandLists[] = { uploadCommandList.Get() };
		commandQueue.ExecuteCommandLists(FANG_COUNT_OF(commandLists), commandLists);
		fence.WaitForGPU(commandQueue);

		entry.descriptorIndex = descriptorIndex;

		D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		viewDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.Texture2D.MipLevels     = 1;

		device.CreateShaderResourceView(
			entry.resource.Get(),
			&viewDesc,
			descriptorHeap.GetCPUHandle(entry.descriptorIndex)
		);

		entry.isAlive = true;

		for (uint32_t index = 0; index < static_cast<uint32_t>(m_entries.size()); ++index)
		{
			if (!m_entries[index].isAlive)
			{
				entry.generation = m_entries[index].generation + 1;
				m_entries[index] = entry;
				return TextureHandle{ index, entry.generation };
			}
		}

		m_entries.push_back(entry);
		return TextureHandle{ static_cast<uint32_t>(m_entries.size() - 1), entry.generation };
	}

	void TexturePool::Destroy(TextureHandle handle)
	{
		if (!handle.IsValid() || handle.index >= m_entries.size())
		{
			return;
		}

		Entry& entry = m_entries[handle.index];
		if (entry.generation != handle.generation)
		{
			return;
		}

		// TODO: ディスクリプタのスロットも返す（Phase 3 でリングバッファにするときに）。
		entry.resource.Reset();
		entry.isAlive = false;
	}

	void TexturePool::Shutdown()
	{
		m_entries.clear();
	}

	const TexturePool::Entry& TexturePool::Get(TextureHandle handle) const
	{
		FANG_ASSERT(handle.IsValid() && handle.index < m_entries.size(), "無効なテクスチャハンドル");

		const Entry& entry = m_entries[handle.index];
		FANG_ASSERT(entry.isAlive && entry.generation == handle.generation, "解放済みのテクスチャハンドル");

		return entry;
	}
} // namespace fang::rhi
