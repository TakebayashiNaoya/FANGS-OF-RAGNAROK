/**
 * @file UnlitRenderer.h
 * @brief 頂点色をそのまま出す非ライティングのレンダラ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Matrix4x4.h"
#include "RHI/RHIHandles.h"


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
	/**
	 * @brief 頂点色を光もテクスチャも掛けずにそのまま描く。
	 * @details 契約は position + color の頂点と、b0 に差す合成済み行列 1 本だけ。深度テストを持たないので
	 *          積んだ順にそのまま上へ重なる ➡ 当たり判定や向きを絵の上に重ねて見せる DebugDraw の土台になる。
	 *          幾何は今のところ組み込みの三角形 1 枚だけで、外から頂点を渡す口は DebugDraw を足す段で作る。
	 *          頂点の並びはシェーダとの契約なので、この中で決めて外へ出さない。
	 *          ビューポートは持たないので、呼び出し側が描画の前に CommandList::SetViewport を済ませておくこと。
	 * @threading メインスレッドのみ。
	 */
	class UnlitRenderer
	{
	public:
		FANG_NON_COPYABLE(UnlitRenderer);

		UnlitRenderer() = default;

		/**
		 * @brief シェーダとステートの組、頂点バッファ、定数の置き場を作る。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device);

		/** @brief パイプラインとバッファを解放する。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/**
		 * @brief 組み込みの三角形 1 枚を、開いているフレームに積む。
		 * @param device      定数バッファを書き込むために使う。
		 * @param commandList BeginFrame が返したコマンドリスト。
		 * @param transform   頂点をクリップ空間へ移す合成済み行列。恒等行列なら頂点の座標がそのまま通る。
		 * @details const にしていないのは、定数バッファの中身を書き換えるため。
		 */
		void DrawTriangle(rhi::GraphicsDevice& device, rhi::CommandList& commandList, const Matrix4x4& transform);


	private:
		rhi::PipelineHandle m_pipeline;             /**< 頂点色用のシェーダとステートの組。 */
		rhi::BufferHandle   m_vertexBuffer;         /**< 組み込みの三角形 3 頂点。 */
		rhi::BufferHandle   m_objectConstantBuffer; /**< b0 に差す行列の置き場。 */
	};
} // namespace fang
