/**
 * @file PipelineState.cpp
 * @brief パイプラインステートを管理するクラスの実装
 */
#include "PipelineState.h"
#include "Core/Log.h"


namespace fangEngine::render
{
	bool PipelineState::Initialize(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineStateDesc)
	{
		// パイプラインステートを作る
		// この中でシェーダーをGPU向けに変換するため、フレームの中で呼ぶとそれを待ってフレームが止まる。
		// 初期化かロードのときにだけ呼ぶ。
		HRESULT result = device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_pipelineState));
		if (core::LogIfFailed(result, L"パイプラインステートの作成に失敗しました。")) return false;

		return true;
	}
} // namespace fangEngine::render
