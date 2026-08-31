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
} // namespace fang::rhi
