/**
 * @file TerrainRenderer.h
 * @brief ハイトマップ地形のチャンクを GPU に載せ、TerrainPass で描くレンダラ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Frustum.h"
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
	 * @brief 地形チャンク 1 個ぶんの入力。
	 * @details Resource 側の生成結果（TerrainChunkSource）と同じ並びだが、Renderer は Resource を参照しない
	 *          （モジュールの依存方向を保つ）ので、受け口の型をこちらでも持つ。詰め替えは呼び出し側の仕事。
	 *          span はこの呼び出しの間だけ読む。
	 */
	struct TerrainChunk
	{
		std::span<const Vector3>  positions; /**< ワールド座標。地形はワールド行列を持たない。 */
		std::span<const Vector3>  normals;   /**< 正規化済みのワールド法線。 */
		std::span<const uint16_t> indices;
		Aabb                      bounds; /**< ワールド空間の箱。フラスタムカリングに使う。 */
	};

	/**
	 * @brief 地形の見た目の設定。CreateTerrain にチャンクと一緒に渡す。
	 * @details テクスチャのハンドルは借用で、所有権は呼び出し側に残る（解放も呼び出し側）。
	 */
	struct TerrainSurface
	{
		rhi::TextureHandle splatmap;        /**< RGBA8。RGB が 3 レイヤの重み。 */
		rhi::TextureHandle layerAlbedos[3]; /**< レイヤのアルベド。0 = 草、1 = 岩、2 = 土の順で焼いてある。 */

		float layerRoughness[3] = { 1.0f, 1.0f, 1.0f }; /**< レイヤごとの知覚 roughness。 */

		/** @brief レイヤのテクスチャ 1 枚がワールドで受け持つ辺長（cm）。小さいほど細かくタイリングする。 */
		float layerTileCentimeters = 400.0f;

		float halfWidth = 0.0f; /**< 地形の X 半幅（cm）。スプラット UV の写像に使う。 */
		float halfDepth = 0.0f; /**< 地形の Z 半幅（cm）。 */

		/** @brief スプラットマップの 1 辺のテクセル数（正方形）。端の半テクセルクランプに使う。 */
		uint32_t splatTexelCount = 0;
	};

	/**
	 * @brief ハイトマップ地形を描く。チャンクの頂点を圧縮して GPU に載せ、視錐台で絞って描く。
	 * @details 頂点の並びはシェーダとの契約なので、この中で決めて外へ出さない。b0（TerrainConstants）は
	 *          地形が動かないので CreateTerrain で 1 回書くだけ。b1 はシーン View の MeshFrameConstants を
	 *          借りる ➡ 視点・光・影パラメータが建物と常に一致する。影は受けるだけで、シャドウ View には
	 *          出さない ➡ カリングで消えたキャスタの影がちらつく構造問題が起きない。
	 * @threading Initialize / Shutdown / CreateTerrain / AddPass はメインスレッドのみ。記録（TerrainPass）は
	 *            RenderGraph のジョブから呼ばれる。userData の実体はメンバに持たせ、RenderGraph::Execute が
	 *            終わるまで生かす。GetLastDrawnChunkCount は Execute の Wait が済んでから読むこと。
	 */
	class TerrainRenderer
	{
	public:
		FANG_NON_COPYABLE(TerrainRenderer);

		/**
		 * @brief 持てるチャンクの数。
		 * @details 実機 60fps のためチャンクは数十のオーダーに収める前提。生成側の分割数がこれを超える
		 *          条件は CreateTerrain がエラーにする。
		 */
		static constexpr uint32_t MAX_CHUNK_COUNT = 64;

		TerrainRenderer() = default;

		/**
		 * @brief パイプライン（4 枚のテクスチャ + WRAP サンプラ + シャドウマップ + 深度テスト）と b0 を作る。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device);

		/** @brief パイプラインと作ったバッファを解放する。借用のテクスチャは解放しない。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/**
		 * @brief チャンクを圧縮頂点で GPU に載せ、b0 に地形の定数を書く。
		 * @param chunks  生成済みのチャンク列。この呼び出しの間だけ読む。
		 * @param surface テクスチャと材質。ハンドルは借用として持ち続けるので、呼び出し側が寿命を保証すること。
		 * @return 失敗したら false。途中で失敗したぶんはこの中で解放し、地形なしの状態に戻る。
		 * @details 2 回呼ぶと前の地形を解放して作り直す。
		 */
		[[nodiscard]] bool CreateTerrain(
			rhi::GraphicsDevice&          device,
			std::span<const TerrainChunk> chunks,
			const TerrainSurface&         surface
		);

		/**
		 * @brief 地形を描く TerrainPass を宣言する。
		 * @param graph               宣言先。
		 * @param backBuffer          色の描画先として登録済みのリソース番号（Load）。
		 * @param depthBuffer         深度の描画先として登録済みのリソース番号（Load）。
		 * @param shadowMapResource   ImportDepthTexture で登録済みのシャドウマップの番号。読むリソースとして
		 *                            宣言する ➡ ShadowPass との前後のバリアは Compile が導く。
		 * @param frameConstantBuffer b1 に差すシーン View の定数バッファ（SceneRenderer::GetFrameConstantBuffer）。
		 *                            中身は AddView が書き込み済みであること。
		 * @param shadowMap           差すシャドウマップの実体（SceneRenderer::GetShadowMapTexture）。
		 * @param viewProjection      カリング用の合成済み行列。シーン View と同じものを渡すこと。
		 * @details チャンクが無い（CreateTerrain 前・失敗後）なら何もしない。
		 */
		void AddPass(
			RenderGraph&          graph,
			RenderGraphResourceId backBuffer,
			RenderGraphResourceId depthBuffer,
			RenderGraphResourceId shadowMapResource,
			rhi::BufferHandle     frameConstantBuffer,
			rhi::TextureHandle    shadowMap,
			const Matrix4x4&      viewProjection
		);

		/** @brief 地形を持っているか。false なら AddPass が何もしない状態。 */
		[[nodiscard]] FANG_FORCEINLINE bool HasTerrain() const { return m_chunkCount > 0; }

		/**
		 * @brief 直近の Execute で実際に描いたチャンクの数。
		 * @details カリングで飛ばした分は数えない。RenderGraph::Execute の Wait が済んでから読むこと。
		 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetLastDrawnChunkCount() const { return m_drawnChunkCount; }


	private:
		/** @brief チャンク 1 個ぶんの GPU リソース。 */
		struct Chunk
		{
			rhi::BufferHandle vertexBuffer; /**< 位置 + 圧縮法線を詰めた 1 本。 */
			rhi::BufferHandle indexBuffer;  /**< 16 bit のインデックス。 */
			uint32_t          indexCount = 0;
			Aabb              bounds; /**< ワールド空間の箱。記録ジョブがこれで絞る。 */
		};

		/** @brief TerrainPass の記録関数に渡す入力。ジョブの中で組み立てずに済むよう POD で揃える。 */
		struct PassRecordArguments
		{
			TerrainRenderer*     renderer = nullptr;
			rhi::GraphicsDevice* device   = nullptr;
		};

		/** @brief TerrainPass の記録関数の入口。RenderGraph に渡せるよう関数ポインタの形にしてある。 */
		static void RecordTerrainPass(void* userData, rhi::CommandList& commandList);

		/** @brief パイプラインと定数とテクスチャを一度差し、視錐台に掛かるチャンクだけ描く。 */
		void RecordChunks(rhi::CommandList& commandList);

		/** @brief 持っているチャンクの GPU バッファを全部解放する。 */
		void DestroyChunks(rhi::GraphicsDevice& device);

		rhi::PipelineHandle m_pipeline;
		rhi::BufferHandle   m_constantBuffer; /**< b0（TerrainConstants）の置き場。ロード時に 1 回書く。 */

		Chunk    m_chunks[MAX_CHUNK_COUNT];
		uint32_t m_chunkCount = 0;

		TerrainSurface m_surface; /**< テクスチャのハンドルは借用。所有権は呼び出し側。 */

		Frustum m_frustum; /**< AddPass が viewProjection から抽出した、このフレームの 6 平面。 */

		rhi::BufferHandle  m_frameConstantBuffer; /**< AddPass が控えた b1。実体は SceneRenderer が持つ。 */
		rhi::TextureHandle m_shadowMap;           /**< AddPass が控えたシャドウマップ。実体は SceneRenderer が持つ。 */

		rhi::GraphicsDevice* m_device = nullptr; /**< 借用ポインタ。記録関数へ渡すために持つ。 */

		PassRecordArguments m_passRecordArguments; /**< AddPass が埋める userData の実体。 */

		uint32_t m_drawnChunkCount = 0; /**< RecordChunks が書く、直近フレームの描画チャンク数。 */
	};
} // namespace fang
