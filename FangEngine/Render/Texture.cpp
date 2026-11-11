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
		// 1行のバイト数は1024行×16バイト＝16,384バイトで、1行の幅はRowPitchの16,384バイト。（256の倍数なので問題なし）
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

		return true;
	}
} // namespace fangEngine::render

