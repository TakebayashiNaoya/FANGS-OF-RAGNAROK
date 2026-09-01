/**
 * @file EditorUI.h
 * @brief ImGui のエディタ UI。
 */
#pragma once

#include "Core/CoreMacros.h"
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
		 * @param device フォントテクスチャとパイプラインの生成に使う。初期化済みであること。
		 * @param window 入力の取り込み先。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device, const Window& window);

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
		[[nodiscard]] bool InitializeBackend(rhi::GraphicsDevice& device);
		void               ShutdownBackend(rhi::GraphicsDevice& device);
		void RenderDrawData(rhi::GraphicsDevice& device, rhi::CommandList& commandList, const ImDrawData& drawData);

		[[nodiscard]] bool EnsureBufferCapacity(rhi::GraphicsDevice& device, const ImDrawData& drawData);
		void               CopyDrawData(rhi::GraphicsDevice& device, const ImDrawData& drawData);

		void BuildEngineInfoWindow(const Window& window, float deltaTimeSeconds);


	private:
		rhi::PipelineHandle m_pipeline;           /**< ImGui 描画用のパイプライン。 */
		rhi::TextureHandle  m_fontTexture;        /**< フォントアトラス。ImGui が焼いたビットマップの転送先。 */
		rhi::BufferHandle   m_vertexBuffer;       /**< 毎フレーム書き換える動的頂点バッファ。 */
		rhi::BufferHandle   m_indexBuffer;        /**< 毎フレーム書き換える動的インデックスバッファ。 */
		uint32_t            m_vertexCapacity = 0; /**< 今のバッファに入る頂点数。足りなくなったら作り直す。 */
		uint32_t            m_indexCapacity  = 0; /**< 今のバッファに入るインデックス数。足りなくなったら作り直す。 */

		// TODO: フレームアロケータができたら差し替える（Phase 2）。
		std::vector<ImDrawVert> m_vertexStaging; /**< 全描画リストの頂点をまとめて 1 回で転送するための作業領域。 */
		std::vector<ImDrawIdx>  m_indexStaging;  /**< 同上のインデックス版。 */

		bool m_isInitialized       = false; /**< Initialize が通ったか。Shutdown の二重呼び出しを弾く。 */
		bool m_isDemoWindowVisible = false; /**< ImGui のデモウィンドウを出すか。機能の見本市。 */
	};
} // namespace fang::editor
