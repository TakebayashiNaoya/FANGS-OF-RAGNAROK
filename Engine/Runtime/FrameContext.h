/**
 * @file FrameContext.h
 * @brief 更新と描画へ渡す、そのフレームだけのものの束。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include <cstddef>
#include <cstdint>


namespace fang
{
	class FrameAllocator;
	class Window;
} // namespace fang


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
	/**
	 * @brief 平行光 1 本。ここに置くのは向きと色だけの値で、光源というオブジェクトは無い。
	 * @details Game が毎フレーム FrameData へ書き、Runtime が描画側へ写す。一度も書かれなければ
	 *          この既定値の光で描かれる（従来の見た目に近い向きにしてある）。
	 */
	struct DirectionalLight
	{
		Vector3 directionToLight = { 0.309f, 0.722f, -0.619f }; /**< 面から光源へ向かう向き。正規化して渡す。 */
		Vector3 color            = { 1.0f, 1.0f, 1.0f };        /**< リニア空間の色。 */
		float   intensity        = 3.14159265f;           /**< BRDF の 1/π を打ち消して従来の明るさに合わせた値。 */
		Vector3 ambientColor     = { 0.2f, 0.2f, 0.22f }; /**< 環境項。光の裏側の形を読ませる役。 */
	};

	/** @brief 更新が作り、次のフレームの描画が読むデータ。 */
	struct FrameData
	{
		DirectionalLight light; /**< このフレームの平行光。将来は昼夜サイクルがここへ書く。 */
	};

#if FANG_ENABLE_PROFILER
	/**
	 * @brief パス 1 つの GPU 時間。
	 * @details 名前は RenderGraph の宣言から写す。宣言の string_view は Execute までしか生きていないため。
	 */
	struct RenderPassGpuTime
	{
		static constexpr size_t MAX_NAME_LENGTH = 32;

		char  name[MAX_NAME_LENGTH] = {}; /**< 終端付き。長い名前は切る。 */
		float milliseconds          = 0.0f;
	};
#endif

	/** @brief 直近に完了した Execute の統計。RenderStatisticsPanel が読む。1 フレーム遅れの値。 */
	struct RenderStatistics
	{
		uint32_t submittedItemCount     = 0; /**< Submit の合計（全 View、カリング前）。 */
		uint32_t drawnItemCount         = 0; /**< 実際に描いた合計（全 View）。 */
		uint32_t drawnTerrainChunkCount = 0; /**< 地形で実際に描いたチャンクの数（カリング後）。 */
		uint32_t passCount              = 0; /**< RenderGraph に宣言されたパスの数。 */
		uint32_t commandListCount       = 0; /**< Execute が記録したコマンドリストの本数。 */

#if FANG_ENABLE_PROFILER
		/** @brief 時間を出せるパスの数。RenderGraph::MAX_PASS_COUNT と揃える（Application.cpp が確かめる）。 */
		static constexpr uint32_t MAX_TIMED_PASS_COUNT = 8;

		RenderPassGpuTime passGpuTimes[MAX_TIMED_PASS_COUNT]; /**< パス順。timedPassCount まで有効。 */
		uint32_t          timedPassCount       = 0;
		float             gpuFrameMilliseconds = 0.0f;  /**< 先頭パスの開始から末尾パスの終了まで。 */
		bool              hasGpuTimestamps     = false; /**< false ならパネルは「なし」と出す。 */

		float recordMilliseconds  = 0.0f; /**< RenderFrame の入口から EndFrame を呼ぶ手前まで。 */
		float presentMilliseconds = 0.0f; /**< ExecuteCommandLists と Present。 */
		float gpuWaitMilliseconds = 0.0f; /**< WaitForGPU。 */
#endif
	};

	/**
	 * @brief 更新の側へ渡す、このフレームだけのもの。ジョブの中で読む。
	 * @details ウィンドウも RHI も入れていないので、ワーカースレッドから触れる先はここにあるものだけになる。
	 */
	struct FrameUpdateContext
	{
		FrameAllocator& frameAllocator; /**< 今のフレームの置き場。書いてよいのはここだけ。 */
		uint64_t        frameIndex       = 0;
		float           deltaTimeSeconds = 0.0f; /**< 1 周の実時間。更新と描画へ同じ値を渡す。 */
	};

	/** @brief 描画の側へ渡す、このフレームだけのもの。メインスレッドで読む。 */
	struct FrameRenderContext
	{
		rhi::GraphicsDevice& device;
		rhi::CommandList&    commandList;
		const Window&        window;

		const FrameData*        frameData = nullptr;  /**< 1 つ前のフレームの更新が作ったもの。無ければ nullptr。 */
		const RenderStatistics& renderStatistics;     /**< 1 つ前のフレームの Execute が書いた統計。 */
		uint64_t                frameIndex       = 0; /**< 描いているフレーム。更新側より 1 小さい。 */
		float                   deltaTimeSeconds = 0.0f;
	};
} // namespace fang
