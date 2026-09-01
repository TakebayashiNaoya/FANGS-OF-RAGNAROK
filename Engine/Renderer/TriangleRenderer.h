/**
 * @file TriangleRenderer.h
 * @brief 三角形 1 枚だけを描く暫定レンダラ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "RHI/RHIHandles.h"
#include <cstdint>


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi

namespace fang
{
	/**
	 * @brief 三角形を 1 枚描く。
	 * @details Phase 1 で RHI の疎通を見るためだけのもの。Phase 3 で RenderGraph に置き換える。
	 * @threading メインスレッドのみ。
	 */
	class TriangleRenderer
	{
	public:
		FANG_NON_COPYABLE(TriangleRenderer);

		TriangleRenderer() = default;

		/**
		 * @brief シェーダーをコンパイルしてパイプラインと頂点バッファを作る。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device);

		/** @brief パイプラインと頂点バッファを解放する。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/**
		 * @brief 開いているフレームに描画コマンドを積む。
		 * @param commandList BeginFrame が返したコマンドリスト。
		 * @param width       描画先の横幅（ピクセル）。ビューポートに使う。
		 * @param height      描画先の高さ（ピクセル）。
		 */
		void Draw(rhi::CommandList& commandList, uint32_t width, uint32_t height) const;


	private:
		rhi::PipelineHandle m_pipeline;     /**< 三角形用のシェーダとステートの組。 */
		rhi::BufferHandle   m_vertexBuffer; /**< 頂点 3 個分の固定バッファ。 */
	};
} // namespace fang
