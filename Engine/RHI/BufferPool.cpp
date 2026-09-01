/**
 * @file BufferPool.cpp
 * @brief 頂点・インデックスバッファの台帳の実装。
 */
#include "Pch.h"
#include "RHI/BufferPool.h"
#include "Core/Log/Assert.h"
#include <cstring>


namespace fang::rhi
{
	BufferHandle BufferPool::Create(ID3D12Device& device,
									const void*   data,
									uint32_t      sizeInBytes,
									uint32_t      strideInBytes,
									EnBufferKind  kind)
	{
		const BufferHandle handle = CreateDynamic(device, sizeInBytes, strideInBytes, kind);
		if (handle.IsValid())
		{
			Update(handle, data, sizeInBytes);
		}

		return handle;
	}

	BufferHandle BufferPool::CreateDynamic(ID3D12Device& device,
										   uint32_t      capacityInBytes,
										   uint32_t      strideInBytes,
										   EnBufferKind  kind)
	{
		// Phase 1 はアップロードヒープに置いたままにする。既定ヒープへの転送は Phase 3。
		Entry entry;
		if (!CreateUploadBuffer(&device, capacityInBytes, entry.resource))
		{
			return BufferHandle{};
		}

		void*       mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		if (!CheckHresult(entry.resource->Map(0, &readRange, &mapped), "バッファの Map"))
		{
			return BufferHandle{};
		}

		entry.mappedPointer   = static_cast<uint8_t*>(mapped);
		entry.capacityInBytes = capacityInBytes;
		entry.kind            = kind;
		entry.isAlive         = true;

		if (kind == EnBufferKind::Vertex)
		{
			entry.vertexBufferView.BufferLocation = entry.resource->GetGPUVirtualAddress();
			entry.vertexBufferView.SizeInBytes    = capacityInBytes;
			entry.vertexBufferView.StrideInBytes  = strideInBytes;
		}
		else
		{
			entry.indexBufferView.BufferLocation = entry.resource->GetGPUVirtualAddress();
			entry.indexBufferView.SizeInBytes    = capacityInBytes;
			entry.indexBufferView.Format         = strideInBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(m_entries.size()); ++index)
		{
			if (!m_entries[index].isAlive)
			{
				entry.generation = m_entries[index].generation + 1;
				m_entries[index] = entry;
				return BufferHandle{ index, entry.generation };
			}
		}

		m_entries.push_back(entry);
		return BufferHandle{ static_cast<uint32_t>(m_entries.size() - 1), entry.generation };
	}

	void BufferPool::Update(BufferHandle handle, const void* data, uint32_t sizeInBytes)
	{
		FANG_ASSERT(handle.IsValid() && handle.index < m_entries.size(), "無効なバッファハンドル");

		Entry& entry = m_entries[handle.index];
		FANG_ASSERT(entry.isAlive && entry.generation == handle.generation, "解放済みのバッファハンドル");
		FANG_ASSERT(sizeInBytes <= entry.capacityInBytes, "バッファの容量を超えて書き込もうとしている");

		std::memcpy(entry.mappedPointer, data, sizeInBytes);
	}

	void BufferPool::Destroy(BufferHandle handle)
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

		if (entry.mappedPointer != nullptr)
		{
			entry.resource->Unmap(0, nullptr);
			entry.mappedPointer = nullptr;
		}

		entry.resource.Reset();
		entry.isAlive = false;
	}

	void BufferPool::Shutdown()
	{
		// Map したままの資源も混ざるが、Unmap しないまま解放してよい（D3D12 が面倒を見る）。
		m_entries.clear();
	}

	const BufferPool::Entry& BufferPool::Get(BufferHandle handle) const
	{
		FANG_ASSERT(handle.IsValid() && handle.index < m_entries.size(), "無効なバッファハンドル");

		const Entry& entry = m_entries[handle.index];
		FANG_ASSERT(entry.isAlive && entry.generation == handle.generation, "解放済みのバッファハンドル");

		return entry;
	}
} // namespace fang::rhi
