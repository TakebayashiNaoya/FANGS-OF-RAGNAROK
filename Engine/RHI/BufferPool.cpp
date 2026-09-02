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
	namespace
	{
		/** @brief 定数バッファの大きさと GPU アドレスに要る境界。D3D12 の決まりで 256 バイト。 */
		constexpr uint32_t CONSTANT_BUFFER_ALIGNMENT = 256;

		/** @brief value を alignment の倍数へ切り上げる。alignment は 2 のべき乗であること。 */
		[[nodiscard]] uint32_t AlignUp(uint32_t value, uint32_t alignment)
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}
	} // namespace


	BufferHandle BufferPool::Create(
		ID3D12Device& device,
		const void*   data,
		uint32_t      sizeInBytes,
		uint32_t      strideInBytes,
		EnBufferKind  kind
	)
	{
		const BufferHandle handle = CreateDynamic(device, sizeInBytes, strideInBytes, kind);
		if (handle.IsValid())
		{
			Update(handle, data, sizeInBytes);
		}

		return handle;
	}


	BufferHandle BufferPool::CreateDynamic(
		ID3D12Device& device,
		uint32_t      capacityInBytes,
		uint32_t      strideInBytes,
		EnBufferKind  kind
	)
	{
		// 定数バッファは 256 バイト境界でないと CBV が作れない。切り上げた端は書かないまま残る。
		const uint32_t allocationSize =
			kind == EnBufferKind::Constant ? AlignUp(capacityInBytes, CONSTANT_BUFFER_ALIGNMENT) : capacityInBytes;

		// 今はアップロードヒープに置いたままにする。既定ヒープへの転送は後回し。
		Entry entry;
		if (!CreateUploadBuffer(&device, allocationSize, entry.resource))
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
		entry.capacityInBytes = allocationSize;
		entry.kind            = kind;
		entry.isAlive         = true;

		switch (kind)
		{
			case EnBufferKind::Vertex:
				entry.vertexBufferView.BufferLocation = entry.resource->GetGPUVirtualAddress();
				entry.vertexBufferView.SizeInBytes    = allocationSize;
				entry.vertexBufferView.StrideInBytes  = strideInBytes;
				break;

			case EnBufferKind::Index:
				entry.indexBufferView.BufferLocation = entry.resource->GetGPUVirtualAddress();
				entry.indexBufferView.SizeInBytes    = allocationSize;
				entry.indexBufferView.Format         = strideInBytes == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
				break;

			case EnBufferKind::Constant:
				// ビューを作らない。ルート CBV は GPU アドレスを直接受け取る。
				break;
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
