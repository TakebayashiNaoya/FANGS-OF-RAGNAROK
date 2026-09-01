/**
 * @file Application.cpp
 * @brief フレームループと初期化順。
 */
#include "Pch.h"
#include "Runtime/Application.h"
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

		if (!application.OnInitialize(device, window))
		{
			FANG_FATAL("上の層の初期化に失敗した");
		}

		FANG_LOG_INFO(Runtime, "フレームループを開始");

		// TODO: Core/Platform に時間を測る口を作る（Phase 2）。
		auto previousTime = std::chrono::steady_clock::now();

		// ウィンドウを閉じるまでループする。WM_QUIT を受け取ると PumpMessages() が false を返す。
		while (window.PumpMessages())
		{
			// 今の時間。
			const auto currentTime = std::chrono::steady_clock::now();
			// 前回からの経過時間（秒）。
			const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - previousTime).count();

			previousTime = currentTime;

			if (window.ConsumeSizeChange())
			{
				device.Resize(window.GetWidth(), window.GetHeight());
			}

			// TODO: 更新と描画を別スレッドに分け、1 フレームずらして並走させる（Phase 2）。
			application.OnUpdate(window, deltaTimeSeconds);

			rhi::CommandList* commandList = device.BeginFrame(BACKGROUND_COLOR);
			if (commandList == nullptr)
			{
				break;
			}

			triangleRenderer.Draw(*commandList, window.GetWidth(), window.GetHeight());
			application.OnRender(device, *commandList);

			device.EndFrame();
		}

		FANG_LOG_INFO(Runtime, "フレームループを終了");

		application.OnShutdown(device);
		triangleRenderer.Shutdown(device);
		device.Shutdown();
		window.Shutdown();

		return 0;
	}
} // namespace fang
