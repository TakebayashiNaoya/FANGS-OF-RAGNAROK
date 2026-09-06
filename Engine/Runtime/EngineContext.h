/**
 * @file EngineContext.h
 * @brief 上の層へ渡すエンジンの持ち物。
 */
#pragma once

namespace fang::rhi
{
	struct ShaderReloadStatus;
} // namespace fang::rhi


namespace fang
{
	class CollisionWorld;
	class FrameClock;
	class FrameMemory;
	class FramePipeline;
	class HeightmapTerrain;
	class JobSystem;
	class MeshRenderer;
	class PlatformBudget;

	/**
	 * @brief RunApplication が持っているものを、上の層から使えるようにまとめた束。
	 * @details 参照で持つのは、渡す側が起動から終了まで生かし続けると決まっていて、null を確かめる分岐が
	 *          要らないため。後から増えるものは、ここへメンバを 1 行足す。
	 */
	struct EngineContext
	{
		JobSystem&   jobSystem;
		FrameMemory& frameMemory;

		/** @brief 1 周の並びと所要時間。上の層は数字を読むだけなので const で渡す。 */
		const FramePipeline& framePipeline;

		/** @brief 実時間の測定元。上限・切られた周の数を上の層が読む。測るのはメインだけなので const。 */
		const FrameClock& frameClock;

		/** @brief Xbox の予算に対する現在値。エディタから倍率と制限の入切を触るので const にしない。 */
		PlatformBudget& platformBudget;

		/** @brief 狼・置き物のメッシュを読み込むために使う。Initialize に失敗していても存在し、黙って空描きする。 */
		MeshRenderer& meshRenderer;

		/** @brief 当たり判定の登録・クエリに使う。作れなかったときだけ nullptr（登録・可視化を飛ばす）。 */
		CollisionWorld* collisionWorld = nullptr;

		/** @brief 接地の高さの問い合わせ先。読めていなければ nullptr（接地せず y = 0 に置く）。 */
		const HeightmapTerrain* terrain = nullptr;

#if FANG_ENABLE_HOT_RELOAD
		/** @brief 直近のシェーダーの作り直しの結果。GraphicsDevice が持っているものを借りる。 */
		const rhi::ShaderReloadStatus* shaderReloadStatus = nullptr;
#endif
	};
} // namespace fang
