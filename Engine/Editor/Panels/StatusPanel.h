/**
 * @file StatusPanel.h
 * @brief 更新ジョブが写した実行中の値を、読み取り専用で並べるパネル。
 */
#pragma once

#include "Core/CoreMacros.h"


namespace fang
{
	struct StatusRowList;
} // namespace fang


namespace fang::editor
{
	/**
	 * @brief 「実行中の値」ウィンドウ 1 枚。更新ジョブが写した行を 1 対 1 で並べる。
	 * @details 実体を 1 つも読まない。読むのは描画が受け取った FrameData の写しだけ（ADR-060）。書く口も無い。
	 * @threading 組み立てはメインスレッドのみ。
	 */
	class StatusPanel
	{
	public:
		FANG_NON_COPYABLE(StatusPanel);

		StatusPanel()  = default;
		~StatusPanel() = default;

		/** @return 失敗しない。他のパネルと戻り値の形をそろえるため bool を返す。 */
		[[nodiscard]] bool Initialize();

		/** @brief 何もしない。他のパネルと呼び出しの形をそろえるため置いてある。 */
		void Shutdown();

		/**
		 * @brief このフレームの内容を組み立てる。ImGui::NewFrame の後に呼ぶ。
		 * @param statusRows 描いているフレームの更新が写した行。無ければ nullptr（1 行だけ出す）。
		 */
		void BuildFrame(const StatusRowList* statusRows);
	};
} // namespace fang::editor
