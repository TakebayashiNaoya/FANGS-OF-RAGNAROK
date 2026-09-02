/**
 * @file Application.cpp
 * @brief フレームループと初期化順。
 */
#include "Pch.h"
#include "Runtime/Application.h"
#include "Core/Job/JobSystem.h"
#include "Core/Log/Assert.h"
#include "Core/Platform/Window.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/TriangleRenderer.h"
#include "Runtime/RuntimeLog.h"
#include <chrono>


FANG_DEFINE_LOG_CATEGORY(Runtime);


namespace fang
{
	namespace
	{
		constexpr rhi::ClearColor BACKGROUND_COLOR{ 0.05f, 0.06f, 0.09f, 1.0f };
	} // namespace


	int RunApplication(IApplication& application)
	{
		// ジョブシステムはここが持ち、使う側へ参照で渡す。Engine ができたらそこへぶら下げ直す。
		JobSystem jobSystem;
		if (!jobSystem.Initialize(JobSystemDesc{}))
		{
			FANG_FATAL("ジョブシステムを開始できなかった");
		}

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

		// 全部の初期化が終わってから束ねる。上の層はここで受けた参照を持ち続ける。
		const EngineContext context{ jobSystem };
		if (!application.OnInitialize(context, device, window))
		{
			FANG_FATAL("上の層の初期化に失敗した");
		}

		FANG_LOG_INFO(Runtime, "フレームループを開始");

		// TODO: Core/Platform に時間を測る口を作る。
		auto previousTime = std::chrono::steady_clock::now();

		// ウィンドウを閉じるまでループする。WM_QUIT を受け取ると PumpMessages() が false を返す。
		while (window.PumpMessages())
		{
			// 前フレームからの経過時間を秒で計算する。
			const auto  currentTime      = std::chrono::steady_clock::now();
			const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - previousTime).count();
			previousTime                 = currentTime;

			// ウィンドウのサイズが変わったら、GPU 側のバックバッファもリサイズする。
			if (window.ConsumeSizeChange())
			{
				device.Resize(window.GetWidth(), window.GetHeight());
			}

			// TODO: 更新と描画を別スレッドに分け、1 フレームずらして並走させる。
			application.OnUpdate(window, deltaTimeSeconds);

			// このフレームの記録準備（記録メモリの巻き戻し、バックバッファの描き込み先への切り替え、クリア）を
			// 頼み、描画コマンドの書き込み先を受け取る。EndFrame まで有効。
			rhi::CommandList* commandList = device.BeginFrame(BACKGROUND_COLOR);
			if (commandList == nullptr)
			{
				break;
			}

			// 三角形を描く。描画コマンドを積むだけで、まだ GPU は動かない。
			triangleRenderer.Draw(*commandList, window.GetWidth(), window.GetHeight());
			// 上の層に描画コマンドを積ませる。
			application.OnRender(device, *commandList);

			device.EndFrame();
		}

		FANG_LOG_INFO(Runtime, "フレームループを終了");

		application.OnShutdown(device);
		triangleRenderer.Shutdown(device);
		device.Shutdown();
		window.Shutdown();
		jobSystem.Shutdown();

		return 0;
	}
} // namespace fang
