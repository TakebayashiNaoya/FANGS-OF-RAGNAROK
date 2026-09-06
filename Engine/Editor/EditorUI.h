/**
 * @file EditorUI.h
 * @brief ImGui のエディタ UI。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Editor/Panels/BudgetPanel.h"
#include "Editor/Panels/JobSystemPanel.h"
#include "Editor/Panels/RenderStatisticsPanel.h"
#include "Editor/Panels/ShaderReloadPanel.h"
#include "Editor/Panels/TuningPanel.h"
#include "RHI/RHIHandles.h"
#include <imgui.h>
#include <vector>


namespace fang
{
	class FramePipeline;
	struct RenderStatistics;
	class Window;
} // namespace fang


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang::editor
{
	/**
	 * @brief エディタ UI 全体。
	 * @details ImGui の呼び出しはこのモジュールの中だけで行う。Release ではリンクされない。
	 *          描画は imgui_impl_dx12 を使わず、RHI の公開 API だけで組んである。
	 * @threading メインスレッドのみ。
	 */
	class EditorUI
	{
	public:
		FANG_NON_COPYABLE(EditorUI);

		EditorUI() = default;
		~EditorUI();

		/**
		 * @brief ImGui のコンテキストと描画バックエンドを作る。
		 * @param context パネルが見るエンジンの持ち物。中身は呼び出し側が生かし続けること。
		 * @param device  フォントテクスチャとパイプラインの生成に使う。初期化済みであること。
		 * @param window  入力の取り込み先。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(const EngineContext& context, rhi::GraphicsDevice& device, const Window& window);

		/** @brief バックエンドとコンテキストを壊す。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/**
		 * @brief 入力を取り込んで、このフレームの UI を組み立てる。
		 * @param deltaTimeSeconds 前フレームからの経過時間（秒）。ImGui のアニメーションと入力判定に使う。
		 * @param renderStatistics 1 つ前のフレームの Execute が書いた描画統計。RenderStatisticsPanel が読む。
		 */
		void BuildFrame(const Window& window, float deltaTimeSeconds, const RenderStatistics& renderStatistics);

		/** @brief 組み立てた UI を積む。BuildFrame と同じフレームで呼ぶ。 */
		void Render(rhi::GraphicsDevice& device, rhi::CommandList& commandList);

		/**
		 * @brief パネルが求めているテスト負荷を走らせる。求められていなければ何もしない。
		 * @param frameIndex 更新しているフレームの番号。描画側と違う面を触るために使う。
		 * @threading 更新ジョブの中（ワーカースレッド）から呼ぶ。ImGui には触らない。
		 */
		void RunRequestedTestLoad(uint64_t frameIndex);


	private:
		/** @brief ImGui を描くための GPU 資源（パイプライン・フォントテクスチャ・動的バッファ）を作る。 */
		[[nodiscard]] bool InitializeBackend(rhi::GraphicsDevice& device);

		/** @brief InitializeBackend で作った資源を返す。 */
		void ShutdownBackend(rhi::GraphicsDevice& device);

		/** @brief ImGui が組んだ描画リストをコマンドリストへ積む。 */
		void RenderDrawData(rhi::GraphicsDevice& device, rhi::CommandList& commandList, const ImDrawData& drawData);

		/** @brief 頂点・インデックスバッファが今の描画量に足りなければ作り直す。 */
		[[nodiscard]] bool EnsureBufferCapacity(rhi::GraphicsDevice& device, const ImDrawData& drawData);

		/** @brief 全描画リストの頂点とインデックスをまとめて動的バッファへ写す。 */
		void CopyDrawData(rhi::GraphicsDevice& device, const ImDrawData& drawData);

		/** @brief フレーム時間などを出すエンジン情報ウィンドウを組み立てる。 */
		void BuildEngineInfoWindow(const Window& window, float deltaTimeSeconds);

		/** @brief パッドの行き先を左上に出す。HasGamepad が立っているときだけ描く。 */
		void BuildGamepadDestinationOverlay();


	private:
		BudgetPanel           m_budgetPanel;           /**< Xbox の予算に対する現在値と、換算・制限の操作。 */
		JobSystemPanel        m_jobSystemPanel;        /**< ジョブシステムの稼働状況。 */
		RenderStatisticsPanel m_renderStatisticsPanel; /**< 描画の中身（Submit数・描いた数・パス数など）。 */
		TuningPanel           m_tuningPanel;           /**< 登録された調整値のつまみ。 */

#if FANG_ENABLE_HOT_RELOAD
		ShaderReloadPanel m_shaderReloadPanel; /**< .hlsl を保存したときの作り直しの結果。 */
#endif

		/** @brief 更新・描画・1 周の所要時間の出どころ。RunApplication が持っているものを借りるだけ。 */
		const FramePipeline* m_framePipeline = nullptr;

		rhi::PipelineHandle m_pipeline;           /**< ImGui 描画用のパイプライン。 */
		rhi::TextureHandle  m_fontTexture;        /**< フォントアトラス。ImGui が焼いたビットマップの転送先。 */
		rhi::BufferHandle   m_vertexBuffer;       /**< 毎フレーム書き換える動的頂点バッファ。 */
		rhi::BufferHandle   m_indexBuffer;        /**< 毎フレーム書き換える動的インデックスバッファ。 */
		uint32_t            m_vertexCapacity = 0; /**< 今のバッファに入る頂点数。足りなくなったら作り直す。 */
		uint32_t            m_indexCapacity  = 0; /**< 今のバッファに入るインデックス数。足りなくなったら作り直す。 */

		// TODO: フレームアロケータができたら差し替える。
		std::vector<ImDrawVert> m_vertexStaging; /**< 全描画リストの頂点をまとめて 1 回で転送するための作業領域。 */
		std::vector<ImDrawIdx>  m_indexStaging;  /**< 同上のインデックス版。 */

		bool m_isInitialized       = false; /**< Initialize が通ったか。Shutdown の二重呼び出しを弾く。 */
		bool m_isDemoWindowVisible = false; /**< ImGui のデモウィンドウを出すか。機能の見本市。 */
	};
} // namespace fang::editor
