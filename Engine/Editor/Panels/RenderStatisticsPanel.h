/**
 * @file RenderStatisticsPanel.h
 * @brief 描画の中身（Submit数・描いた数・パス数・コマンドリスト本数）を出すパネル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <cstdint>


namespace fang
{
	struct RenderStatistics;
} // namespace fang


namespace fang::editor
{
	/**
	 * @brief 「レンダリング統計」ウィンドウ 1 枚。
	 * @details フレーム時間（ms）と FPS は自前の移動平均、残り 4 値（Submit 数・描いた数・パス数・コマンド
	 *          リスト本数）は BuildFrame が受けた RenderStatistics をそのまま表示する。カメラを回して
	 *          描いた数が Submit 数より減ることを見せる、カリングの可視化道具。
	 * @threading 組み立て・移動平均の更新ともメインスレッドのみ。
	 */
	class RenderStatisticsPanel
	{
	public:
		FANG_NON_COPYABLE(RenderStatisticsPanel);

		/** @brief 移動平均に使う履歴の長さ（フレーム数）。 */
		static constexpr uint32_t FRAME_TIME_HISTORY_LENGTH = 120;

		RenderStatisticsPanel()  = default;
		~RenderStatisticsPanel() = default;

		/**
		 * @brief 移動平均の入れ物を 0 に戻す。
		 * @details 履歴は固定長配列なので確保は要らない。呼ぶ理由は入れ物の初期化のためだけ。
		 * @return 失敗しない。JobSystemPanel など他のパネルと戻り値の形をそろえるため bool を返す。
		 */
		[[nodiscard]] bool Initialize();

		/** @brief 移動平均の入れ物を 0 に戻す。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief このフレームの内容を組み立てる。ImGui::NewFrame の後に呼ぶ。
		 * @param deltaTimeSeconds  前フレームからの経過時間（秒）。フレーム時間・FPS の移動平均へ積む。
		 * @param renderStatistics  1 つ前のフレームの Execute が書いた統計。Runtime のメインスレッドが
		 *                          安全な地点で読んだ値をそのまま渡してくる。
		 */
		void BuildFrame(float deltaTimeSeconds, const RenderStatistics& renderStatistics);


	private:
		/**
		 * @brief フレーム時間の履歴へ 1 件積み、移動和を更新する。
		 * @details 押し出す値を和から引く ➡ 新しい値を書いて和に足す ➡ 書き込み位置を進める、の O(1)。
		 */
		void PushFrameTimeSample(float deltaTimeSeconds);

		/** @brief 移動平均のフレーム時間（ms）と FPS を組み立てる。 */
		void BuildFrameTimeSection() const;

		/** @brief Submit 数・描いた数・パス数・コマンドリスト本数をそのまま並べる。 */
		void BuildRenderStatisticsSection(const RenderStatistics& renderStatistics) const;


	private:
		float m_frameTimeSecondsHistory[FRAME_TIME_HISTORY_LENGTH]{}; /**< リングバッファ。秒単位。 */

		uint32_t m_frameTimeHistoryWriteIndex  = 0; /**< 次に書き込む位置。 */
		uint32_t m_frameTimeHistorySampleCount = 0; /**< 埋まった数。起動直後は分母をこれに絞る。 */

		float m_frameTimeMovingSumSeconds = 0.0f; /**< 履歴の合計。サンプル数で割ると移動平均になる。 */
	};
} // namespace fang::editor
