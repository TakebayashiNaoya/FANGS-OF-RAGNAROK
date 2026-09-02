/**
 * @file EditorUI.h
 * @brief ImGui のエディタ UI。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Editor/Panels/JobSystemPanel.h"
#include "RHI/RHIHandles.h"
#include <imgui.h>
#include <vector>


namespace fang
{
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
		 */
		void BuildFrame(const Window& window, float deltaTimeSeconds);

		/** @brief 組み立てた UI を積む。BuildFrame と同じフレームで呼ぶ。 */
		void Render(rhi::GraphicsDevice& device, rhi::CommandList& commandList);


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


	private:
		JobSystemPanel m_jobSystemPanel; /**< ジョブシステムの稼働状況。 */

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
