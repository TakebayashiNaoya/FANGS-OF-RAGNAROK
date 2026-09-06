/**
 * @file FrameContext.h
 * @brief 更新と描画へ渡す、そのフレームだけのものの束。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Input/Gamepad.h"
#include <cstddef>
#include <cstdint>
#include <span>


namespace fang
{
	class FrameAllocator;
	class Window;
	struct ColliderProxy;
	struct RenderItem;
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

	/**
	 * @brief カメラ 1 台の視点。
	 * @details Game が CameraFollowParameter から毎フレーム計算して FrameData へ書く。縦横比は Runtime が
	 *          ウィンドウの大きさから足す（Game はウィンドウを知らないため）。
	 */
	struct CameraView
	{
		Vector3 eyePosition;
		Vector3 targetPosition;
		float   fieldOfViewYRadians = 0.0f; /**< 0 のままなら、書かれていない印として Runtime が既定値を使う。 */
	};

	/** @brief 更新が作り、次のフレームの描画が読むデータ。 */
	struct FrameData
	{
		DirectionalLight light;  /**< このフレームの平行光。将来は昼夜サイクルがここへ書く。 */
		CameraView       camera; /**< このフレームのカメラ。書かれていなければ既定のカメラで描く。 */

		/** @brief このフレームに描くもの。Scene::BuildRenderItems がフレームメモリへ組み立てたもの。 */
		std::span<const RenderItem> renderItems;

		/**
		 * @brief このフレームの当たり判定の可視化用。Scene::BuildColliderProxies が組み立てたもの。
		 * @details 当たり判定そのものは Game が別途 CollisionWorld::Update へ渡す。ここにあるのは
		 *          デバッグ描画がワイヤーを起こすための写し。
		 */
		std::span<const ColliderProxy> colliderProxies;
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
		float           deltaTimeSeconds = 0.0f; /**< 1 周の刻み。上限で切ってある。 */

		/**
		 * @brief 起動からの経過（秒）。切っていない実時間。
		 * @details 昼夜サイクルのように実時間へ追随するものはこれを読む。積む値ではなく絶対の時刻なので、
		 *          刻みとして使い回すことができない ➡ 素の実時間が刻みの経路へ漏れない。
		 */
		double elapsedSeconds = 0.0;

		/**
		 * @brief このフレームのパッド。
		 * @details ReadGamepadState はメインスレッドのみなので、周の頭でメインが読んでここへ書く。
		 */
		GamepadState gamepad;
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
