/**
 * @file Texture.cpp
 * @brief テクスチャを管理するクラスの実装
 */
#include "Texture.h"
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

		HRESULT result = renderContext.GetDevice()->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_resource)
		);
		if (core::LogIfFailed(result, L"テクスチャバッファの作成に失敗しました。")) return false;

		return true;
	}
} // namespace fangEngine::render

