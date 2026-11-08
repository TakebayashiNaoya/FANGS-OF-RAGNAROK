/**
 * @file PipelineState.h
 * @brief パイプラインステートを管理するクラス
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"


namespace fangEngine::render
{
	/**
	 * @brief パイプラインステートを管理するクラス
	 * @details 設定はパスごとに違うため、呼ぶ側が組み立てて渡す。
	 *          GPUのオブジェクトを持つためコピー禁止。
	 */
	class PipelineState : private core::Noncopyable
	{
	public:
		/**
		 * @brief パイプラインステートを初期化する
		 * @param device デバイス
		 * @param pipelineStateDesc パイプラインステートの設定
		 * @return 初期化に成功した場合はtrue、失敗した場合はfalse
		 */
		bool Initialize(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineStateDesc);

		/**
		 * @brief パイプラインステートを返す
		 * @return パイプラインステート
		 */
		inline ID3D12PipelineState* GetPipelineState() const { return m_pipelineState.Get(); }


	private:
		Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
	};
} // namespace fangEngine::render
