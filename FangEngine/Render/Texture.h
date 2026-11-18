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
		struct UploadLayout;

		/**
		 * @brief テクスチャの入れ物をDEFAULTのヒープに作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @param textureData テクスチャのデータ
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateTextureResource(ID3D12Device* device, const TextureData& textureData);

		/**
		 * @brief ミップごとの置き方をGPUに聞き、テクスチャの一時置き場をUPLOADのヒープに作る
		 * @param device GPUのオブジェクトを作るためのID3D12Device
		 * @param uploadLayout 一時置き場でのミップごとの置き方を受け取る入れ物
		 * @param uploadBuffer 作った一時置き場を受け取る入れ物
		 * @return 作成に成功したらtrue、失敗したらfalse
		 */
		bool CreateUploadBuffer(ID3D12Device* device, UploadLayout& uploadLayout, Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

		/**
		 * @brief 一時置き場にピクセルを1行ずつ写す
		 * @param uploadBuffer 写し先の一時置き場
		 * @param uploadLayout 一時置き場でのミップごとの置き方
		 * @param textureData 写し元のテクスチャのデータ
		 * @return 写せたらtrue、マップに失敗したらfalse
		 */
		bool CopyPixelsToUploadBuffer(ID3D12Resource* uploadBuffer, const UploadLayout& uploadLayout, const TextureData& textureData);

		/**
		 * @brief 一時置き場からテクスチャへのコピーと、シェーダーが読む状態への切り替えをリストに予約する
		 * @param commandList 予約するコマンドリスト
		 * @param uploadBuffer 写し元の一時置き場
		 * @param uploadLayout 一時置き場でのミップごとの置き方
		 */
		void RecordCopy(ID3D12GraphicsCommandList* commandList, ID3D12Resource* uploadBuffer, const UploadLayout& uploadLayout);

		/**
		 * @brief シェーダーがテクスチャを読むための窓口（SRV）を作る
		 * @param renderContext レンダリングコンテキスト
		 * @param textureData テクスチャのデータ
		 * @return 作成に成功したらtrue、SRVの置き場所が足りなければfalse
		 */
		bool CreateShaderResourceView(RenderContext& renderContext, const TextureData& textureData);


	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_resource; // 色のデータを入れるテクスチャ
		D3D12_GPU_DESCRIPTOR_HANDLE            m_gpuHandle{}; // シェーダーがSRVを探す場所
	};
} // namespace fangEngine::render
