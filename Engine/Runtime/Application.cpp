/**
 * @file Application.cpp
 * @brief フレームループと初期化順。
 */
#include "Pch.h"
#include "Runtime/Application.h"
#include "Core/Job/JobSystem.h"
#include "Core/Log/Assert.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/Window.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/TriangleRenderer.h"
#include "Runtime/FramePipeline.h"
#include "Runtime/RuntimeLog.h"
#include <chrono>


FANG_DEFINE_LOG_CATEGORY(Runtime);


namespace fang
{
	namespace
	{
		constexpr rhi::ClearColor BACKGROUND_COLOR{ 0.05f, 0.06f, 0.09f, 1.0f };

		/** @brief FramePipeline へ渡す、フレームループの持ち物。 */
		struct FrameLoopContext
		{
			IApplication*        application      = nullptr;
			rhi::GraphicsDevice* device           = nullptr;
			Window*              window           = nullptr;
			TriangleRenderer*    triangleRenderer = nullptr;

			/** @brief バックバッファを取れなかったフレームで立つ。ループを抜ける合図。 */
			bool hasDeviceError = false;
		};

		/** @brief 更新の本体。ワーカースレッドで走るので、渡された束の外へは手を伸ばさない。 */
		FrameData* UpdateFrame(void* userData, const FrameUpdateContext& context)
		{
			auto& loopContext = *static_cast<FrameLoopContext*>(userData);
			return loopContext.application->OnUpdate(context);
		}

		/** @brief 描画の本体。RHI を触るのはここだけなので、メインスレッドの持ち物が全部そろっている。 */
		void RenderFrame(void* userData, const FrameData* frameData, uint64_t frameIndex, float deltaTimeSeconds)
		{
			auto& loopContext = *static_cast<FrameLoopContext*>(userData);

			rhi::GraphicsDevice& device = *loopContext.device;
			Window&              window = *loopContext.window;

			// リサイズは更新と関わらないので、ジョブを投げた後のここで済ませる。BeginFrame の中では作り直せない。
			if (window.ConsumeSizeChange())
			{
				device.Resize(window.GetWidth(), window.GetHeight());
			}

			// このフレームの記録準備（記録メモリの巻き戻し、バックバッファの描き込み先への切り替え、クリア）を
			// 頼み、描画コマンドの書き込み先を受け取る。EndFrame まで有効。
			rhi::CommandList* commandList = device.BeginFrame(BACKGROUND_COLOR);
			if (commandList == nullptr)
			{
				loopContext.hasDeviceError = true;
				return;
			}

			// 三角形を描く。描画コマンドを積むだけで、まだ GPU は動かない。
			loopContext.triangleRenderer->Draw(*commandList, window.GetWidth(), window.GetHeight());

			// 上の層に描画コマンドを積ませる。読ませるのは 1 つ前のフレームの更新が作ったもの。
			const FrameRenderContext context{ device, *commandList, window, frameData, frameIndex, deltaTimeSeconds };
			loopContext.application->OnRender(context);

			device.EndFrame();
		}
	} // namespace


	int RunApplication(IApplication& application)
	{
		// ジョブシステムはここが持ち、使う側へ参照で渡す。Engine ができたらそこへぶら下げ直す。
		JobSystem jobSystem;
		if (!jobSystem.Initialize(JobSystemDesc{}))
		{
			FANG_FATAL("ジョブシステムを開始できなかった");
		}

		// フレームメモリもここが持つ。JobSystem と同じく、使う側へは参照で渡す。
		FrameMemory frameMemory;
		if (!frameMemory.Initialize(FrameMemoryDesc{}))
		{
			FANG_FATAL("フレームメモリを確保できなかった");
		}

		FANG_LOG_INFO(
			Runtime,
			"フレームメモリを確保した (1 枚 {} KiB × {} 枚)",
			frameMemory.GetCapacityPerBuffer() / 1024,
			FrameMemory::BUFFER_COUNT
		);

		Window window;
		if (!window.Initialize(WindowDesc{}))
		{
			FANG_FATAL("ウィンドウを作れなかった");
		}

		rhi::GraphicsDeviceDesc deviceDesc{};
		deviceDesc.windowHandle        = window.GetNativeHandle();
		deviceDesc.width               = window.GetWidth();
		deviceDesc.height              = window.GetHeight();
		deviceDesc.isDebugLayerEnabled = FANG_ENABLE_GPU_VALIDATION != 0;

		rhi::GraphicsDevice device;
		if (!device.Initialize(deviceDesc))
		{
			FANG_FATAL("D3D12 デバイスを作れなかった");
		}

		TriangleRenderer triangleRenderer;
		if (!triangleRenderer.Initialize(device))
		{
			FANG_FATAL("三角形の準備に失敗した");
		}

		FrameLoopContext loopContext{};
		loopContext.application      = &application;
		loopContext.device           = &device;
		loopContext.window           = &window;
		loopContext.triangleRenderer = &triangleRenderer;

		FramePipeline framePipeline;
		if (!framePipeline.Initialize(jobSystem, frameMemory, &loopContext, &UpdateFrame, &RenderFrame))
		{
			FANG_FATAL("フレームパイプラインを組めなかった");
		}

		// 全部の初期化が終わってから束ねる。上の層はここで受けた参照を持ち続ける。
		const EngineContext context{ jobSystem, frameMemory, framePipeline };
		if (!application.OnInitialize(context, device, window))
		{
			FANG_FATAL("上の層の初期化に失敗した");
		}

		FANG_LOG_INFO(Runtime, "フレームループを開始");

		// 1 周目に描く相手を作っておく。
		framePipeline.Prime();

		// TODO: Core/Platform に時間を測る口を作る。
		auto previousTime = std::chrono::steady_clock::now();

		// ウィンドウを閉じるまでループする。WM_QUIT を受け取ると PumpMessages() が false を返す。
		while (!loopContext.hasDeviceError && window.PumpMessages())
		{
			// 前フレームからの経過時間を秒で計算する。更新と描画のどちらにも同じ値を渡す。
			const auto  currentTime      = std::chrono::steady_clock::now();
			const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - previousTime).count();
			previousTime                 = currentTime;

			framePipeline.RunFrame(deltaTimeSeconds);
		}

		FANG_LOG_INFO(Runtime, "フレームループを終了");

		framePipeline.Shutdown();
		application.OnShutdown(device);
		triangleRenderer.Shutdown(device);
		device.Shutdown();
		window.Shutdown();

		// フレームメモリはジョブから触られるので、ワーカーを畳んでから返す。
		jobSystem.Shutdown();
		frameMemory.Shutdown();

		return 0;
	}
} // namespace fang
