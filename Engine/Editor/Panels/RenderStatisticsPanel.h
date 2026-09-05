/**
 * @file RenderStatisticsPanel.h
 * @brief 描画の中身（Submit数・描いた数・パス数・コマンドリスト本数）と、描画時間の内訳を出すパネル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Runtime/FrameContext.h"
#include <cstdint>


namespace fang::editor
{
	/**
	 * @brief 「レンダリング統計」ウィンドウ 1 枚。
	 * @details フレーム時間（ms）と FPS、描画（メイン）の内訳、パス別の GPU 時間は自前の移動平均で出す。
	 *          件数（Submit 数・描いた数・パス数・コマンドリスト本数）は BuildFrame が受けた RenderStatistics
	 *          をそのまま表示する。カメラを回して描いた数が Submit 数より減ることと、そのときにどのパスの
	 *          GPU 時間が動くかを見せる道具。
	 * @threading 組み立て・移動平均の更新ともメインスレッドのみ。
	 */
	class RenderStatisticsPanel
	{
	public:
		FANG_NON_COPYABLE(RenderStatisticsPanel);

		/** @brief 移動平均に使う履歴の長さ（フレーム数）。全部の行で共有する。 */
		static constexpr uint32_t HISTORY_LENGTH = 120;

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
		 * @brief 固定長の履歴と移動和。
		 * @details 押し出す値を和から引く ➡ 新しい値を書いて和に足す ➡ 書き込み位置を進める、の O(1)。
		 * @threading メインスレッドのみ。
		 */
		struct MovingAverage
		{
			/** @brief 1 件積む。 */
			void Push(float value);

			/** @brief 今の平均。まだ 1 件も無ければ 0。 */
			[[nodiscard]] float Get() const;

			/** @brief 空に戻す。別の条件で出た値を混ぜないために呼ぶ。 */
			void Reset();

			float    history[HISTORY_LENGTH]{}; /**< リングバッファ。 */
			uint32_t writeIndex  = 0;           /**< 次に書き込む位置。 */
			uint32_t sampleCount = 0;           /**< 埋まった数。起動直後は分母をこれに絞る。 */
			float    movingSum   = 0.0f;        /**< 履歴の合計。サンプル数で割ると移動平均になる。 */
		};

		/** @brief 全部の移動平均を空に戻す。Initialize と Shutdown の中身。 */
		void ResetAverages();

		/** @brief 移動平均のフレーム時間（ms）と FPS を組み立てる。 */
		void BuildFrameTimeSection() const;

		/** @brief Submit 数・描いた数・パス数・コマンドリスト本数をそのまま並べる。 */
		void BuildRenderStatisticsSection(const RenderStatistics& renderStatistics) const;

#if FANG_ENABLE_PROFILER
		/** @brief 内訳と GPU 時間を移動平均へ積む。パス名が変わった番号は履歴を捨ててから積む。 */
		void PushTimingSamples(const RenderStatistics& renderStatistics);

		/** @brief 描画（メイン）の内訳 3 行を組み立てる。 */
		void BuildRenderTimeBreakdownSection() const;

		/** @brief GPU 合計とパス別の行を組み立てる。取れない環境は「なし」の 1 行。 */
		void BuildGpuTimeSection(const RenderStatistics& renderStatistics) const;
#endif


	private:
		MovingAverage m_frameTimeSeconds; /**< 秒単位。FPS も同じ平均から出す。 */

#if FANG_ENABLE_PROFILER
		MovingAverage m_recordMilliseconds;
		MovingAverage m_presentMilliseconds;
		MovingAverage m_gpuWaitMilliseconds;
		MovingAverage m_gpuFrameMilliseconds;

		MovingAverage m_passGpuMilliseconds[RenderStatistics::MAX_TIMED_PASS_COUNT]; /**< パスの番号ごと。 */

		/** @brief 番号ごとに控えた前フレームのパス名。違う名前が来たら、その番号の履歴を捨てる。 */
		char m_passNames[RenderStatistics::MAX_TIMED_PASS_COUNT][RenderPassGpuTime::MAX_NAME_LENGTH]{};
#endif
	};
} // namespace fang::editor
