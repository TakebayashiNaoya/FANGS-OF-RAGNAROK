/**
 * @file CommandList.h
 * @brief 描画コマンドの記録先。
 */
#pragma once

#include "RHI/RHIHandles.h"
#include <cstdint>


namespace fang::rhi
{
	class GraphicsDevice;

	/**
	 * @brief 1 フレーム分のコマンドを積む口。
	 * @details 実体は GraphicsDevice が持つ。BeginFrame が返したポインタは EndFrame まで有効。
	 * @threading 受け取ったスレッドのみ。並列記録は Phase 3 で複数本に分ける。
	 */
	class CommandList
	{
	public:
		/**
		 * @brief ビューポートとシザーを画面全体に合わせる。
		 * @param width 描画先の横幅（ピクセル）。ふつうはバックバッファと同じ値を渡す。
		 * @param height 描画先の高さ（ピクセル）。
		 */
		void SetViewport(uint32_t width, uint32_t height);

		/**
		 * @brief シザー矩形だけを変える。矩形の外のピクセルは描かれない。
		 * @param left 左端（ピクセル座標）。この位置を含む。
		 * @param top 上端。この位置を含む。
		 * @param right 右端。この位置は含まない。
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
		 * @param values 書き込む中身。この呼び出しの間だけ読む。
		 * @param count32BitValues 書き込む個数（32 bit 単位）。パイプライン作成時の rootConstantCount 以下。
		 */
		void SetRootConstants(const void* values, uint32_t count32BitValues);

		/**
		 * @brief t0 にテクスチャを差す。
		 * @param texture 差すテクスチャ。hasTexture かつ rootConstantCount > 0 で作ったパイプラインが前提。
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

		GraphicsDevice* m_device  = nullptr;
		void* m_nativeCommandList = nullptr;
	};
} // namespace fang::rhi
