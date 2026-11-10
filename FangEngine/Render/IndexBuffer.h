/**
 * @file IndexBuffer.h
 * @brief インデックスバッファを管理するクラス
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "Core/Noncopyable.h"


namespace fangEngine::render
{
	/**
	 * @brief インデックスバッファを管理するクラス
	 * @details GPUのオブジェクトを持つためコピー禁止。
	 */
	class IndexBuffer : private core::Noncopyable
	{
	public:
		/**
		 * @brief インデックスバッファを作る
		 * @param device インデックスバッファを作るためのID3D12Device
		 * @param indices インデックスデータ
		 * @param size インデックス全部のバイト数
		 * @param format インデックスのフォーマット
		 * @return インデックスバッファを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(ID3D12Device* device, const void* indices, UINT size, DXGI_FORMAT format);

		/**
		 * @brief インデックスバッファのビューを返す
		 * @return インデックスバッファのビュー
		 */
		inline const D3D12_INDEX_BUFFER_VIEW& GetView() const { return m_view; }


	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_resource; // インデックスのデータを入れるバッファ
		D3D12_INDEX_BUFFER_VIEW                m_view{};   // GPUにバッファの読み方を伝える札
	};
} // namespace fangEngine::render
