/**
 * @file DebugOverlay.h
 * @brief デバッグ用オーバーレイ描画クラス
 */
#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
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

		/**
		 * @brief ディスクリプタを割り当てる
		 * @param cpuHandle CPU側のディスクリプタハンドル
		 * @param gpuHandle GPU側のディスクリプタハンドル
		 */
		void AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);

		/**
		 * @brief ディスクリプタを解放する
		 * @param cpuHandle CPU側のディスクリプタハンドル
		 */
		void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);


	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shaderResourceViewDescriptorHeap;

		std::vector<UINT> m_freeDescriptorIndices;             // 空いている番号の一覧
		UINT              m_descriptorHandleIncrementSize = 0; // ディスクリプタ1つ分の大きさ
	};
} // namespace fangEngine::render
