/**
 * @file VertexBuffer.h
 * @brief 頂点バッファを管理するクラス
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"


namespace fangEngine::render
{
	/**
	 * @brief 頂点バッファを管理するクラス
	 * @details GPUのオブジェクトを持つためコピー禁止。
	 */
	class VertexBuffer : private core::Noncopyable
	{
	public:
		/**
		 * @brief 頂点バッファを作る
		 * @param device 頂点バッファを作るためのID3D12Device
		 * @param vertices 頂点データ
		 * @param size 頂点全部のバイト数
		 * @param stride 1頂点のバイト数
		 * @return 頂点バッファを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(ID3D12Device* device, const void* vertices, UINT size, UINT stride);

		/**
		 * @brief 頂点バッファのビューを返す
		 * @return 頂点バッファのビュー
		 */
		inline const D3D12_VERTEX_BUFFER_VIEW& GetView() const { return m_view; }


	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_resource; // 頂点のデータを入れるバッファ
		D3D12_VERTEX_BUFFER_VIEW               m_view{};   // GPUにバッファの読み方を伝える札
	};
} // namespace fangEngine::render
