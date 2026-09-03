/**
 * @file DebugDraw.h
 * @brief 線分だけを積んで描くデバッグ描画。
 */
#pragma once

#if FANG_ENABLE_DEBUG_DRAW

#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "RHI/RHIHandles.h"
#include "Renderer/RenderGraph.h"
#include <cstdint>
#include <span>


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
	/**
	 * @brief 当たり判定の箱やシャドウの視錐台など、ワールド空間の線分を積んで 1 パスで描く。
	 * @details 頂点は位置 + 色で UnlitVS / UnlitPS をそのまま再利用し、LINELIST・深度テストのみ
	 *          （書き込みなし）で描く ➡ メッシュには隠れつつ、線同士は積んだ順にそのまま重なる。
	 *          持ち物は MAX_LINE_COUNT ぶんの固定長配列 ➡ 定常状態のヒープ確保は 0。
	 * @threading Initialize / Shutdown / Reset / AddLine 系 / AddPass はメインスレッドのみ。記録
	 *            （DebugLinePass）は RenderGraph のジョブから呼ばれる。userData の実体はメンバに
	 *            持たせ、RenderGraph::Execute が終わるまで生かす。
	 */
	class DebugDraw
	{
	public:
		FANG_NON_COPYABLE(DebugDraw);

		/** @brief 1 フレームに積める線分の本数。 */
		static constexpr uint32_t MAX_LINE_COUNT = 4096;

		DebugDraw() = default;

		/**
		 * @brief パイプライン（LINELIST・深度テストのみ）と、動的な頂点・定数バッファを作る。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device);

		/** @brief パイプラインとバッファを解放する。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/** @brief 積んだ本数をフレームの頭で 0 に戻す。ヒープ確保はしない。 */
		void Reset();

		/**
		 * @brief 線分を 1 本積む。
		 * @param from  始点のワールド座標。
		 * @param to    終点のワールド座標。
		 * @param color 頂点色（0.0〜1.0）。アルファは 1.0 に固定する。
		 * @details MAX_LINE_COUNT を使い切っていたら FANG_LOG_WARNING を出して捨てる。
		 */
		void AddLine(const Vector3& from, const Vector3& to, const Vector3& color);

		/**
		 * @brief 軸平行の境界ボックスをワイヤーで積む。
		 * @param bounds 描く箱。IsValid() が false なら何もしない（呼び出し側に分岐を要らなくする）。
		 * @param color  頂点色。
		 */
		void AddWireBox(const Aabb& bounds, const Vector3& color);

		/**
		 * @brief 軸に沿わない箱を 8 頂点で積む。
		 * @param corners 箱の 8 頂点。並びは Aabb::GetCorners と同じ規則（0〜3 が近い面、4〜7 が奥の面）。
		 * @param color   頂点色。
		 * @details シャドウの光の視錐台のように、軸平行でない箱のために持つ。要素数が 8 でなければ FANG_ASSERT。
		 */
		void AddWireBoxCorners(std::span<const Vector3> corners, const Vector3& color);

		/**
		 * @brief 座標軸を積む。
		 * @param world  原点と向きを決める行列。
		 * @param length 1 本の長さ（cm）。
		 * @details X を赤、Y を緑、Z を青で描く。
		 */
		void AddAxes(const Matrix4x4& world, float length);

		/**
		 * @brief 積んだ線分を描く DebugLinePass を宣言する。
		 * @param graph          宣言先。
		 * @param backBuffer     色の描画先として登録済みのリソース番号（Load）。
		 * @param depthBuffer    深度の描画先として登録済みのリソース番号（Load、テストのみで書き込まない）。
		 * @param viewProjection 線をワールド空間からクリップ空間へ移す合成済み行列。
		 * @details 積んだ本数が 0 でも空のパスを宣言してよい。記録関数が頂点 0 本で早期リターンする。
		 */
		void AddPass(
			RenderGraph&          graph,
			RenderGraphResourceId backBuffer,
			RenderGraphResourceId depthBuffer,
			const Matrix4x4&      viewProjection
		);


	private:
		/**
		 * @brief シェーダに渡す頂点。
		 * @details UnlitVertex と同型だが独立した型。AddLine が積む間だけ生かす CPU 側の固定長配列を
		 *          メンバに持つ必要があるため、UnlitVertex と違って外部に公開しない範囲でヘッダに置く。
		 */
		struct DebugLineVertex
		{
			float position[3]; /**< ワールド座標。 */
			float color[4];    /**< 頂点色。アルファは常に 1.0。 */
		};

		/** @brief DebugLinePass の記録関数に渡す入力。ジョブの中で組み立てずに済むよう POD で揃える。 */
		struct PassRecordArguments
		{
			DebugDraw*           debugDraw = nullptr; /**< 記録先。 */
			rhi::GraphicsDevice* device    = nullptr; /**< バッファ更新に使うデバイス。 */
		};

		/** @brief DebugLinePass の記録関数の入口。RenderGraph に渡せるよう関数ポインタの形にしてある。 */
		static void RecordDebugLinePass(void* userData, rhi::CommandList& commandList);

		/** @brief 積んだ頂点と行列を書き出し、パイプラインとバッファを差して描く。 */
		void RecordLines(rhi::GraphicsDevice& device, rhi::CommandList& commandList);

		rhi::PipelineHandle m_pipeline;       /**< LINELIST・深度テストのみのシェーダとステートの組。 */
		rhi::BufferHandle   m_vertexBuffer;   /**< 動的頂点バッファ。MAX_LINE_COUNT * 2 頂点ぶん。 */
		rhi::BufferHandle   m_constantBuffer; /**< b0 に差す viewProjection の置き場。 */

		rhi::GraphicsDevice* m_device = nullptr; /**< 借用ポインタ。記録関数へ渡すために持つ。 */

		Matrix4x4 m_viewProjection; /**< AddPass が控えた、このフレームの合成済み行列。 */

		DebugLineVertex m_vertices[MAX_LINE_COUNT * 2]; /**< AddLine が書き溜める CPU 側の頂点。 */

		PassRecordArguments m_passRecordArguments; /**< AddPass が埋める userData の実体。 */

		uint32_t m_lineCount = 0; /**< 積んだ線分の本数。Reset が 0 に戻す。 */
	};
} // namespace fang

#endif // FANG_ENABLE_DEBUG_DRAW
