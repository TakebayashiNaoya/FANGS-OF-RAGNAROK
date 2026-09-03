/**
 * @file CommandList.h
 * @brief 描画コマンドの記録先。
 */
#pragma once

#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
#include <cstdint>


namespace fang::rhi
{
	class GraphicsDevice;

	/**
	 * @brief コマンドを積む口 1 本。
	 * @details 実体は GraphicsDevice が持つ。AcquireCommandList が返したポインタは EndFrame まで有効。
	 *          D3D12 のコマンドリストは本をまたいで状態を引き継がないので、描画先もビューポートも本ごとに差し直す。
	 * @threading 1 本につき 1 スレッド。複数スレッドで記録するなら、スレッドごとに別の本を借りる。
	 */
	class CommandList
	{
	public:
		/**
		 * @brief バックバッファの用途を切り替えるバリアを積む。
		 * @details 描画先の状態はフレームをまたいで受け渡されるので、積む場所を一か所に集められる呼び出し側
		 *          （RenderGraph）だけが積む。パスの記録関数からは呼ばない。
		 * @param before 今の用途。直前に宣言したものと食い違うとデバッグレイヤーに叱られる。
		 * @param after  これからの用途。
		 */
		void TransitionBackBuffer(EnResourceState before, EnResourceState after);

		/**
		 * @brief 今のバックバッファを描画先に据える。
		 * @details TransitionBackBuffer と同じ理由で、積むのは RenderGraph だけ。パスの記録関数からは呼ばない。
		 * @param withDepth 深度バッファも一緒に差すか。深度テストをするパイプラインを使うなら true。
		 */
		void SetRenderTargetToBackBuffer(bool withDepth);

		/**
		 * @brief 描画先の色を塗りつぶす。SetRenderTargetToBackBuffer の後に呼ぶ。
		 * @details 積むのは RenderGraph だけ。パスの記録関数からは呼ばない。
		 */
		void ClearRenderTarget(const ClearColor& color);

		/**
		 * @brief 深度を一番奥（1.0）で埋める。SetRenderTargetToBackBuffer に true を渡した後に呼ぶ。
		 * @details 積むのは RenderGraph だけ。パスの記録関数からは呼ばない。
		 */
		void ClearDepth();

		/**
		 * @brief ビューポートとシザーを画面全体に合わせる。
		 * @param width  描画先の横幅（ピクセル）。ふつうはバックバッファと同じ値を渡す。
		 * @param height 描画先の高さ（ピクセル）。
		 */
		void SetViewport(uint32_t width, uint32_t height);

		/**
		 * @brief シザー矩形だけを変える。矩形の外のピクセルは描かれない。
		 * @param left   左端（ピクセル座標）。この位置を含む。
		 * @param top    上端。この位置を含む。
		 * @param right  右端。この位置は含まない。
		 * @param bottom 下端。この位置は含まない。
		 */
		void SetScissor(int32_t left, int32_t top, int32_t right, int32_t bottom);

		/** @brief パイプライン（ルートシグネチャ + PSO）を差す。以降の Set / Draw はこの構成で解釈される。 */
		void SetPipeline(PipelineHandle pipeline);

		/** @brief 頂点バッファを差す。 */
		void SetVertexBuffer(BufferHandle buffer);

		/** @brief インデックスバッファを差す。 */
		void SetIndexBuffer(BufferHandle buffer);

		/**
		 * @brief b0 のルート定数を書く。rootConstantCount > 0 で作ったパイプラインを差してから呼ぶ。
		 * @param values           書き込む中身。この呼び出しの間だけ読む。
		 * @param count32BitValues 書き込む個数（32 bit 単位）。パイプライン作成時の rootConstantCount 以下。
		 */
		void SetRootConstants(const void* values, uint32_t count32BitValues);

		/**
		 * @brief b0 に定数バッファを差す。hasObjectConstantBuffer で作ったパイプラインを差してから呼ぶ。
		 * @param buffer EnBufferKind::Constant で作ったバッファ。中身は UpdateBuffer で先に書いておく。
		 */
		void SetObjectConstantBuffer(BufferHandle buffer);

		/**
		 * @brief b1 に定数バッファを差す。hasFrameConstantBuffer で作ったパイプラインを差してから呼ぶ。
		 * @param buffer EnBufferKind::Constant で作ったバッファ。中身は UpdateBuffer で先に書いておく。
		 */
		void SetFrameConstantBuffer(BufferHandle buffer);

		/**
		 * @brief b2 に定数バッファを差す。hasConstantBuffer で作ったパイプラインを差してから呼ぶ。
		 * @param buffer EnBufferKind::Constant で作ったバッファ。中身は UpdateBuffer で先に書いておく。
		 */
		void SetConstantBuffer(BufferHandle buffer);

		/**
		 * @brief t0 にテクスチャを差す。
		 * @param texture 差すテクスチャ。hasTexture で作ったパイプラインが前提。
		 */
		void SetTexture(TextureHandle texture);

		/**
		 * @brief インデックスなしで描く。
		 * @param vertexCount 頂点バッファの先頭から使う頂点の数。
		 */
		void Draw(uint32_t vertexCount);

		/**
		 * @brief インデックス付きで描く。
		 * @param indexCount 使うインデックスの数。
		 * @param startIndex インデックスバッファの何番目から読み始めるか。
		 * @param baseVertex 読んだインデックスに足す値。複数メッシュを 1 本の頂点バッファに詰めたときに使う。0 でよい。
		 */
		void DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex);


	private:
		friend class GraphicsDevice;

		GraphicsDevice* m_device  = nullptr; /**< 生成元。ハンドルを D3D12 の実体に引くために使う。 */
		void* m_nativeCommandList = nullptr; /**< 実体は ID3D12GraphicsCommandList*。ヘッダに型を出さないため void*。 */

		/** @brief 今差さっているパイプラインのルートパラメータ番号。SetPipeline が入れ替える。 */
		RootParameterLayout m_boundRootParameters;
	};
} // namespace fang::rhi
