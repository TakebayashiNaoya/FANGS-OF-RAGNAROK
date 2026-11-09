/**
 * @file ConstantBuffer.h
 * @brief 定数バッファを管理するクラス
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"
#include "RenderContext.h"


namespace fangEngine::render
{
	/**
	 * @brief 定数バッファを管理するクラス
	 * @details CPUが毎フレーム書いた値を、シェーダーに読ませるためのバッファ。
	 *          GPUが使っている最中の定数バッファをCPUが使わないように、
	 *          RenderContextのRENDER_TARGET_COUNT個分のバッファを持ち、CPUとGPUは定数バッファを交互に使う。
	 *          GPUのオブジェクトを持つためコピー禁止。
	 */
	class ConstantBuffer : private core::Noncopyable
	{
	public:
		/**
		 * @brief 定数バッファを作る
		 * @param device 定数バッファを作るためのID3D12Device
		 * @param size 定数バッファのサイズ(バイト単位)
		 * @return 定数バッファを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(ID3D12Device* device, UINT size);

		/**
		 * @brief 定数バッファの中身をCPU側で作ったデータで書き換える
		 * @param frameIndex 書き換えるフレームの番号
		 * @param data CPU側で作ったデータの番地
		 */
		void Copy(UINT frameIndex, const void* data);

		/**
		 * @brief GPU側の定数バッファの番地を返す
		 * @param frameIndex 取得するフレームの番号
		 * @return GPU側の定数バッファの番地
		 */
		D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress(UINT frameIndex) const;


	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_resources[RenderContext::RENDER_TARGET_COUNT]; // バッファ本体

		void* m_mappedData[RenderContext::RENDER_TARGET_COUNT]{}; // Mapで受け取ったCPUの番地

		UINT m_size = 0; // 呼ぶ側が頼んだ大きさ(写すバイト数)
	};
} // namespace fangEngine::render
