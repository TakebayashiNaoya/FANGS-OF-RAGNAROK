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


	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_resource; // 色のデータを入れるテクスチャ
	};
} // namespace fangEngine::render
