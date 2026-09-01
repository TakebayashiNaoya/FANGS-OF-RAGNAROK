/**
 * @file EditorUI.h
 * @brief ImGui のエディタ UI。
 */
#pragma once

#include "Core/CoreMacros.h"


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
		class Backend;

		void BuildEngineInfoWindow(const Window& window, float deltaTimeSeconds);

		Backend* m_backend = nullptr; /**< ImGui を知る実装本体。ヘッダから imgui.h を追い出すための Pimpl。 */
		bool     m_isDemoWindowVisible = false; /**< ImGui のデモウィンドウを出すか。機能の見本市。 */
	};
} // namespace fang::editor
