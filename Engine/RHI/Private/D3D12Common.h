/**
 * @file D3D12Common.h
 * @brief RHI の内部だけで使う DirectX 12 の共通定義。
 */
#pragma once

#include "RHI/RHILog.h"
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>


namespace fang::rhi
{
	template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	constexpr uint32_t BACK_BUFFER_COUNT = 2;

	/** @brief HRESULT を見てログを出す。失敗したら false。 */
	[[nodiscard]] inline bool CheckHresult(HRESULT result, const char* what)
	{
		if (SUCCEEDED(result))
		{
			return true;
		}

		FANG_LOG_ERROR(RHI, "{} に失敗した (HRESULT=0x{:08X})", what, static_cast<uint32_t>(result));
		return false;
	}

	/** @brief アップロードヒープにバッファを作る。 */
	[[nodiscard]] inline bool CreateUploadBuffer(ID3D12Device*           device,
												 uint32_t                sizeInBytes,
												 ComPtr<ID3D12Resource>& outResource)
	{
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width            = sizeInBytes;
		resourceDesc.Height           = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels        = 1;
		resourceDesc.Format           = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		return CheckHresult(device->CreateCommittedResource(&heapProperties,
															D3D12_HEAP_FLAG_NONE,
															&resourceDesc,
															D3D12_RESOURCE_STATE_GENERIC_READ,
															nullptr,
															IID_PPV_ARGS(&outResource)),
							"アップロードバッファの生成");
	}
} // namespace fang::rhi
