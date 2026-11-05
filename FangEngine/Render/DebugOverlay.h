/**
 * @file DebugOverlay.h
 * @brief デバッグ用オーバーレイ描画クラス
 */
#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"


namespace fangEngine::render
{
	// 前方宣言
	class RenderContext;


	/**
	 * @brief デバッグ用オーバーレイ描画クラス
	 * @details デバッグ用の情報を画面に重ねて描画する。
	 *          RenderContextがImGuiを知らなくて済むように、ImGuiの処理をこのクラスに閉じ込める。
	 */
	class DebugOverlay : private core::Noncopyable
	{
	public:
		/**
		 * @brief ImGuiを動かすためのオブジェクトを作る
		 * @param window ImGuiが入力を受け取るウィンドウ
		 * @param renderContext 描画に使うGPUのオブジェクトを持つRenderContext
		 * @return 全部作れたらtrue、1つでも失敗したらfalse
		 */
		bool Initialize(HWND window, RenderContext& renderContext);


	private:
		/**
		 * @brief シェーダーリソースビューのディスクリプタヒープを作成する
		 * @param device D3D12デバイス
		 * @return 成功した場合はtrue、失敗した場合はfalse
		 */
		bool CreateShaderResourceViewDescriptorHeap(ID3D12Device* device);


	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shaderResourceViewDescriptorHeap;
	};
} // namespace fangEngine::render
