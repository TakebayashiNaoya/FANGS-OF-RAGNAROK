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
	namespace
	{
		/** @brief 2048 テクセルの全ミップは 12 段。余白を持たせた上限で、転送用の配列をスタックに置くため。 */
		constexpr uint32_t MAX_MIP_COUNT = 16;


		DXGI_FORMAT ToDxgiFormat(EnTextureFormat format)
		{
			switch (format)
			{
				case EnTextureFormat::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
				case EnTextureFormat::RGBA8Srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
				case EnTextureFormat::BC7: return DXGI_FORMAT_BC7_UNORM;
				case EnTextureFormat::BC7Srgb: return DXGI_FORMAT_BC7_UNORM_SRGB;
			}

			return DXGI_FORMAT_UNKNOWN;
		}
	} // namespace


	TextureHandle TexturePool::Create(
		ID3D12Device&        device,
		ID3D12CommandQueue&  commandQueue,
		GPUFence&            fence,
		DescriptorHeap&      descriptorHeap,
		const TextureSource& source
	)
	{
		const uint32_t mipCount = static_cast<uint32_t>(source.mipLevels.size());
		if (mipCount == 0 || mipCount > MAX_MIP_COUNT)
		{
			FANG_ASSERT(false, "テクスチャのミップ段数がおかしい");
			return TextureHandle{};
		}

		uint32_t descriptorIndex = 0;
		if (!descriptorHeap.Allocate(descriptorIndex))
		{
			return TextureHandle{};
		}

		D3D12_HEAP_PROPERTIES defaultHeapProperties{};
		defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC textureDesc{};
		textureDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width            = source.mipLevels[0].width;
		textureDesc.Height           = source.mipLevels[0].height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels        = static_cast<UINT16>(mipCount);
		textureDesc.Format           = ToDxgiFormat(source.format);
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

		// アップロードバッファ内での各段の置き場は D3D が決める（256 バイト境界などの都合があるため）。
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[MAX_MIP_COUNT]{};
		UINT                               rowCounts[MAX_MIP_COUNT]{};
		UINT64                             rowSizes[MAX_MIP_COUNT]{};
		UINT64                             uploadSize = 0;
		device.GetCopyableFootprints(&textureDesc, 0, mipCount, 0, footprints, rowCounts, rowSizes, &uploadSize);

		//------------------------------------------------------------------------
		// 1. アップロード用バッファの確保
		// 　CPU から書ける中間バッファ(アップロードヒープ)を必要な大きさぶん確保して Map する。
		// 　各ミップの中身を D3D が計算した行ピッチに合わせて 1 行ずつコピーし終えたら Unmap する。
		//------------------------------------------------------------------------
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

		for (uint32_t mip = 0; mip < mipCount; ++mip)
		{
			const TextureMipLevel& mipLevel = source.mipLevels[mip];

			// 渡された行のバイト数が D3D の見立てと食い違うのは、形式か寸法が合っていないということ。
			// そのまま詰めるとずれた絵が出るので、作らずに引き返す。
			if (mipLevel.pixels == nullptr || mipLevel.rowPitch != rowSizes[mip] ||
				mipLevel.sizeInBytes != mipLevel.rowPitch * rowCounts[mip])
			{
				FANG_ASSERT(false, "ミップの中身が形式・寸法と合っていない");
				uploadBuffer->Unmap(0, nullptr);
				return TextureHandle{};
			}

			// 行ごとのピッチが 256 バイト境界に合わされるので 1 行ずつ詰める。
			const uint8_t* sourceBytes = static_cast<const uint8_t*>(mipLevel.pixels);
			for (UINT row = 0; row < rowCounts[mip]; ++row)
			{
				std::memcpy(
					mapped + footprints[mip].Offset + static_cast<size_t>(row) * footprints[mip].Footprint.RowPitch,
					sourceBytes + static_cast<size_t>(row) * mipLevel.rowPitch,
					mipLevel.rowPitch
				);
			}
		}

		uploadBuffer->Unmap(0, nullptr);

		//------------------------------------------------------------------------
		// 2. 各ミップのコピーコマンド
		// 　転送だけに使う専用のコマンドアロケータとリストを用意し、ミップごとに CopyTextureRegion を積む。
		// 　コピー元はアップロードバッファ内のその段の置き場(footprints)、コピー先はテクスチャのその段。
		//------------------------------------------------------------------------
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

		for (uint32_t mip = 0; mip < mipCount; ++mip)
		{
			D3D12_TEXTURE_COPY_LOCATION copySource{};
			copySource.pResource       = uploadBuffer.Get();
			copySource.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			copySource.PlacedFootprint = footprints[mip];

			D3D12_TEXTURE_COPY_LOCATION destination{};
			destination.pResource        = entry.resource.Get();
			destination.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destination.SubresourceIndex = mip;

			uploadCommandList->CopyTextureRegion(&destination, 0, 0, 0, &copySource, nullptr);
		}

		//------------------------------------------------------------------------
		// 3. PixelShaderResource への遷移
		// 　コピー先が「コピーの受け口」のままだとシェーダから読めない。バリアで用途を切り替えてから
		// 　コマンドリストを Close する。
		//------------------------------------------------------------------------
		// リソースバリア: 「コピーの受け口」から「シェーダが読むもの」へ用途を切り替える宣言。
		// 生成時に COPY_DEST で作ってあるので、転送コマンドの後ろに積んでおけば
		// GPU は転送完了 → 切り替えの順で処理する（詳しい理屈は CommandList の TransitionBackBuffer 側）。
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

		barrier.Transition.pResource   = entry.resource.Get();
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		uploadCommandList->ResourceBarrier(1, &barrier);

		FANG_VERIFY(SUCCEEDED(uploadCommandList->Close()));

		//------------------------------------------------------------------------
		// 4. 実行とフェンス待ち
		// 　積んだコマンドをキューへ渡して実行し、フェンスで GPU の完了を待つ。ここで待ち切ることで、
		// 　この後 SRV を作る時点では転送が必ず終わっている。
		//------------------------------------------------------------------------
		ID3D12CommandList* commandLists[] = { uploadCommandList.Get() };
		commandQueue.ExecuteCommandLists(FANG_COUNT_OF(commandLists), commandLists);
		fence.WaitForGPU(commandQueue);

		//------------------------------------------------------------------------
		// 5. SRV の作成
		// 　確保しておいたディスクリプタの枠へ、テクスチャを読むための SRV を書き込む。
		//------------------------------------------------------------------------
		entry.descriptorIndex = descriptorIndex;

		D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
		viewDesc.Format        = textureDesc.Format;
		viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		viewDesc.Texture2D.MipLevels     = mipCount;

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

		// TODO: ディスクリプタのスロットも返す（リングバッファ化するときに）。
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
