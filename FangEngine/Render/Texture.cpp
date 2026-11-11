/**
 * @file Texture.cpp
 * @brief テクスチャを管理するクラスの実装
 */
#include "Texture.h"
#include <vector>
#include <cstring>
#include "Core/Log.h"
#include "RenderContext.h"


namespace fangEngine::render
{
	bool Texture::Initialize(RenderContext& renderContext, const TextureData& textureData)
	{
		// テクスチャバッファの作成
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width            = textureData.width;
		resourceDesc.Height           = textureData.height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels        = textureData.mipCount;
		resourceDesc.Format           = textureData.format;
		resourceDesc.SampleDesc.Count = 1;
		HRESULT createTextureResult = renderContext.GetDevice()->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_resource)
		);
		if (core::LogIfFailed(createTextureResult, L"テクスチャバッファの作成に失敗しました。")) return false;

		// テクスチャの一時置き場の大きさと、ミップごとの置き方をGPUに聞く。
		// GPUは1行の幅を256バイトの倍数でしか読まないため、.ddsのバイト数のままでは置けない。
		// BC圧縮は4×4ピクセルを16バイトの塊にするため、行と1行のバイト数は塊で数える（4096の段は16,384バイトの行が1,024行）。
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(textureData.mipCount); // ミップごとの置き場所と行の幅
		std::vector<UINT>                               rowCounts(textureData.mipCount);  // ミップごとの行の数
		std::vector<UINT64>                             rowSizes(textureData.mipCount);   // ミップごとの1行のバイト数
		UINT64                                          uploadBufferSize = 0;             // 一時置き場の全体のバイト数
		renderContext.GetDevice()->GetCopyableFootprints(&resourceDesc, 0, textureData.mipCount, 0, footprints.data(), rowCounts.data(), rowSizes.data(), &uploadBufferSize);

		// テクスチャの一時置き場を作る
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC uploadBufferDesc{};
		uploadBufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadBufferDesc.Width            = uploadBufferSize;
		uploadBufferDesc.Height           = 1;
		uploadBufferDesc.DepthOrArraySize = 1;
		uploadBufferDesc.MipLevels        = 1;
		uploadBufferDesc.Format           = DXGI_FORMAT_UNKNOWN;
		uploadBufferDesc.SampleDesc.Count = 1;
		uploadBufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
		HRESULT createUploadBufferResult = renderContext.GetDevice()->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadBuffer)
		);
		if (core::LogIfFailed(createUploadBufferResult, L"テクスチャの一時置き場の作成に失敗しました。")) return false;

		// 一時置き場にピクセルを写す。
		// CPUから書けるよう、一時置き場をCPUのアドレスに見えるようにする（Map）。
		std::uint8_t* mappedData = nullptr;
		D3D12_RANGE readRange{};
		HRESULT mapResult = uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
		if (core::LogIfFailed(mapResult, L"テクスチャの一時置き場のマップに失敗しました。")) return false;

		// .ddsでは行がすき間なく並ぶが、一時置き場では1行がRowPitchの幅（256の倍数）を取る。
		// 並びの幅が違うため、1回のmemcpyでは写せず、1行ずつ置き直す。
		// BC圧縮は4×4ピクセルを16バイトの塊にするため、解像度が4096×4096の段は、1024×1024の塊が並ぶ。
		// 1行のバイト数は1024個×16バイト＝16,384バイトで、1行の幅はRowPitchの16,384バイト。（256の倍数なので問題なし）
		// 解像度が64×64未満の場合、一行が256バイト未満になるため、RowPitchを256バイトにそろえ、先頭を256バイトの倍数に置く必要がある。
		const std::uint8_t* source = textureData.pixels.data(); // .ddsのピクセルの、次に写す位置
		// ミップの段を大きい順（4096、2048、…、1）に回す
		for (UINT mipIndex = 0; mipIndex < textureData.mipCount; mipIndex++)
		{
			// この段の、一時置き場での始まり（Offset）と行の幅（RowPitch）
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = footprints[mipIndex];
			// この段の行を上から回す
			for (UINT rowIndex = 0; rowIndex < rowCounts[mipIndex]; rowIndex++)
			{
				// 写し先は、段の始まりから「行の番号×RowPitch」先。
				// 写し元は、1行写すごとに「.ddsの1行のバイト数」だけ進める。
				std::memcpy(mappedData + footprint.Offset + rowIndex * footprint.Footprint.RowPitch, source, rowSizes[mipIndex]);
				source += rowSizes[mipIndex];
			}
		}
		// 書き終えたため、CPUのアドレスから外す
		uploadBuffer->Unmap(0, nullptr);

		// 一時置き場からテクスチャへのコピーを、段ごとにリストへ予約する
		ID3D12GraphicsCommandList* commandList = renderContext.GetCommandList();
		for (UINT mipIndex = 0; mipIndex < textureData.mipCount; mipIndex++)
		{
			D3D12_TEXTURE_COPY_LOCATION copyDestination{}; // 写し先：m_resourceのmipIndex段目
			copyDestination.pResource        = m_resource.Get();
			copyDestination.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			copyDestination.SubresourceIndex = mipIndex;

			D3D12_TEXTURE_COPY_LOCATION copySource{}; // 写し元：一時置き場の、この段の置き場所
			copySource.pResource       = uploadBuffer.Get();
			copySource.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			copySource.PlacedFootprint = footprints[mipIndex];

			commandList->CopyTextureRegion(&copyDestination, 0, 0, 0, &copySource, nullptr);
		}

		// m_resourceを「コピーの書き込み先」から「シェーダーが読む物」に切り替える
		D3D12_RESOURCE_BARRIER resourceBarrier{};
		resourceBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Transition.pResource   = m_resource.Get();
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		resourceBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		commandList->ResourceBarrier(1, &resourceBarrier);

		// 一時置き場をGPUがコピーしている途中で消すと、消えたメモリを読んでしまうため、GPUの処理が終わるまで保持する
		renderContext.KeepUntilUploaded(uploadBuffer);

		// シェーダーがテクスチャを読むための窓口（SRV）を作る
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
		if (renderContext.AllocateShaderResourceView(&cpuHandle, &m_gpuHandle) == false) return false;

		D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
		shaderResourceViewDesc.Format                  = textureData.format;
		shaderResourceViewDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		shaderResourceViewDesc.Texture2D.MipLevels     = textureData.mipCount;
		renderContext.GetDevice()->CreateShaderResourceView(m_resource.Get(), &shaderResourceViewDesc, cpuHandle);

		return true;
	}
} // namespace fangEngine::render

