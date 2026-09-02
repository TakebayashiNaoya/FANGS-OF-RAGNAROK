/**
 * @file FrameContext.h
 * @brief 更新と描画へ渡す、そのフレームだけのものの束。
 */
#pragma once

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
	/** @brief 更新が作り、次のフレームの描画が読むデータの土台。中身は上の層が決める。 */
	struct FrameData
	{
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

		const FrameData* frameData        = nullptr; /**< 1 つ前のフレームの更新が作ったもの。無ければ nullptr。 */
		uint64_t         frameIndex       = 0;       /**< 描いているフレーム。更新側より 1 小さい。 */
		float            deltaTimeSeconds = 0.0f;
	};
} // namespace fang
