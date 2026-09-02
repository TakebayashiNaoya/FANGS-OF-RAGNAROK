/**
 * @file DepthBuffer.cpp
 * @brief 深度バッファと、その深度ステンシルビューの実装。
 */
#include "Pch.h"
#include "RHI/DepthBuffer.h"


namespace fang::rhi
{
	bool DepthBuffer::Initialize(ID3D12Device& device, uint32_t width, uint32_t height)
	{
		// DSV は 1 個しか要らない。大きさが変わってもヒープはそのまま使えるので、作るのはここだけ。
		D3D12_DESCRIPTOR_HEAP_DESC depthStencilViewHeapDesc{};
		depthStencilViewHeapDesc.NumDescriptors = 1;
		depthStencilViewHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		if (!CheckHresult(
				device.CreateDescriptorHeap(&depthStencilViewHeapDesc, IID_PPV_ARGS(&m_depthStencilViewHeap)),
				"深度ステンシルビューのヒープ生成"
			))
		{
			return false;
		}

		return CreateDepthTexture(device, width, height);
	}


	void DepthBuffer::Resize(ID3D12Device& device, uint32_t width, uint32_t height)
	{
		// 古いテクスチャを掴んだままだと、同じ大きさの領域を二重に抱えることになる。
		m_depthTexture.Reset();

		if (!CreateDepthTexture(device, width, height))
		{
			return;
		}

		FANG_LOG_INFO(RHI, "深度バッファを作り直した ({}x{})", width, height);
	}


	void DepthBuffer::Shutdown()
	{
		m_depthTexture.Reset();
		m_depthStencilViewHeap.Reset();
	}


	D3D12_CPU_DESCRIPTOR_HANDLE DepthBuffer::GetDepthStencilView() const
	{
		return m_depthStencilViewHeap->GetCPUDescriptorHandleForHeapStart();
	}


	bool DepthBuffer::CreateDepthTexture(ID3D12Device& device, uint32_t width, uint32_t height)
	{
		D3D12_HEAP_PROPERTIES defaultHeapProperties{};
		defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC depthTextureDesc{};
		depthTextureDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthTextureDesc.Width            = width;
		depthTextureDesc.Height           = height;
		depthTextureDesc.DepthOrArraySize = 1;
		depthTextureDesc.MipLevels        = 1;
		depthTextureDesc.Format           = DEPTH_FORMAT;
		depthTextureDesc.SampleDesc.Count = 1;
		depthTextureDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthTextureDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// 最適化クリア値。ClearDepthStencilView に渡す値と揃えておく。
		// 渡さないと「クリアが最適化できない」とデバッグレイヤーに警告される。
		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format               = DEPTH_FORMAT;
		clearValue.DepthStencil.Depth   = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		// 深度として使う以外の用途が無いので、生成時から DEPTH_WRITE にしてバリアでの遷移を省く。
		if (!CheckHresult(
				device.CreateCommittedResource(
					&defaultHeapProperties,
					D3D12_HEAP_FLAG_NONE,
					&depthTextureDesc,
					D3D12_RESOURCE_STATE_DEPTH_WRITE,
					&clearValue,
					IID_PPV_ARGS(&m_depthTexture)
				),
				"深度バッファの生成"
			))
		{
			return false;
		}

		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
		viewDesc.Format        = DEPTH_FORMAT;
		viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		device.CreateDepthStencilView(
			m_depthTexture.Get(),
			&viewDesc,
			m_depthStencilViewHeap->GetCPUDescriptorHandleForHeapStart()
		);

		return true;
	}
} // namespace fang::rhi
