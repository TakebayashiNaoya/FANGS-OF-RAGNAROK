/**
 * @file Texture.h
 * @brief テクスチャを管理するクラス
 */
#pragma once
#include <wrl/client.h>
#include "Core/Noncopyable.h"
#include "Render/TextureData.h"


namespace fangEngine::render
{
	// 前方宣言
	class RenderContext;


	/**
	 * @brief テクスチャを管理するクラス
	 * @details GPUのオブジェクトを持つためコピー禁止。
	 */
	class Texture : private core::Noncopyable
	{
	public:
		/**
		 * @brief テクスチャを作る
		 * @param renderContext レンダリングコンテキスト
		 * @param textureData テクスチャのデータ
		 * @return テクスチャを作れたらtrue、作れなかったらfalse
		 */
		bool Initialize(RenderContext& renderContext, const TextureData& textureData);

		/**
		 * @brief テクスチャのGPUのアドレスを取得する
		 * @return テクスチャのGPUのアドレス
		 */
		inline D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return m_gpuHandle; }


	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_resource; // 色のデータを入れるテクスチャ
		D3D12_GPU_DESCRIPTOR_HANDLE            m_gpuHandle{}; // シェーダーがSRVを探す場所
	};
} // namespace fangEngine::render
