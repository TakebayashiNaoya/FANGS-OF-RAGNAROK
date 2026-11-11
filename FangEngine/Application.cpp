/**
 * @file Application.cpp
 * @brief アプリケーションクラスの実装
 */
#include "Application.h"
#include <chrono>
#include "Core/Log.h"
#include "Core/Math/Math.h"


namespace fangEngine
{
	namespace
	{
		constexpr const wchar_t* WINDOW_CLASS_NAME   = L"FangsOfRagnarok_MainWindow"; // ウィンドウクラス名
		constexpr const wchar_t* WINDOW_TITLE        = L"Fangs of Ragnarok";          // ウィンドウ上部に表示されるタイトル
		constexpr int            WINDOW_WIDTH        = 800;
		constexpr int            WINDOW_HEIGHT       = 600;

		constexpr float MAX_DELTA_TIME = 1.0f / 30.0f;

		constexpr float          MODEL_ROTATION_SPEED = core::DegreesToRadians(90.0f);
		constexpr const wchar_t* WOLF_MODEL_PATH = L"Assets/Models/Wolf/Wolf.fmodel";  // 作業フォルダから見た狼のモデルのパス

		/**
		 * @brief WM_DESTROYの場合はループを抜ける指示を出し、それ以外の場合はWindowsの既定の処理に任せる
		 * @return WM_DESTROYの場合は0を返し、それ以外の場合はDefWindowProcWの戻り値を返す
		 */
		LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
		{
			// ImGuiが使ったメッセージは、ここで終わりにする
			if (render::DebugOverlay::HandleMessage(window, message, wParam, lParam)) return true;

			switch (message)
			{
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProcW(window, message, wParam, lParam);
			}
		}
	}


	Application::~Application()
	{
		// DebugOverlayが捨てるものをGPUが読み終えるのを待つ
		m_renderContext.WaitForGpu();

		// メモリを解放する
		m_memory.FreeMemory(m_firstBlock);
		m_memory.FreeMemory(m_secondBlock);
	}


	bool Application::Initialize()
	{
		// Memoryの一覧を確認するためにメモリを確保する
		m_firstBlock  = m_memory.MallocMemory(64, "Application");
		m_secondBlock = m_memory.MallocMemory(32, "Application");

		// ウィンドウを作る
		HWND window = CreateMainWindow();
		if (window == nullptr) return false;

		// RenderContextを作る
		if (m_renderContext.Initialize(window) == false) return false;

		// Modelを作る
		render::ModelData wolfModelData{};
		if (render::LoadModel(WOLF_MODEL_PATH, wolfModelData) == false) return false;

		// モデルを作り、テクスチャのコピーを1回でGPUに流す
		// Texture::Initializeはコピーを積むだけで、EndUploadが流して待つまで中身が入らないため。
		if (m_renderContext.BeginUpload() == false)                          return false;
		if (m_wolfModel.Initialize(m_renderContext, wolfModelData) == false) return false;
		if (m_renderContext.EndUpload() == false)                            return false;

		// ModelPassを作る
		if (m_modelPass.Initialize(m_renderContext.GetDevice()) == false) return false;

		// カメラの行列を作る
		const core::Matrix view       = core::Matrix::LookAt({ 0.0f, 1.5f, -3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
		const core::Matrix projection = core::Matrix::Perspective(core::DegreesToRadians(60.0f), static_cast<float>(WINDOW_WIDTH) / WINDOW_HEIGHT, 0.1f, 100.0f);
		m_viewProjection = view * projection;

		// DebugOverlayを作る
		if (m_debugOverlay.Initialize(window, m_renderContext) == false) return false;

		return true;
	}


	bool Application::Run()
	{
		MSG message{};
		auto previousFrameTime = std::chrono::steady_clock::now();
		bool running = true;
		while (running)
		{
			while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT) {
					running = false;
					break;
				}
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}

			// ウィンドウが閉じられた場合はループを抜ける
			if (running == false) break;

			// 時間の更新処理
			auto currentFrameTime = std::chrono::steady_clock::now();
			float deltaTime = std::chrono::duration<float>(currentFrameTime - previousFrameTime).count();
			if (deltaTime > MAX_DELTA_TIME) {
				deltaTime = MAX_DELTA_TIME;
			}
			previousFrameTime = currentFrameTime;

			// モデルの回転角度を更新する
			m_modelAngle = core::WrapAngle(m_modelAngle + MODEL_ROTATION_SPEED * deltaTime);

			// 描画の開始
			if (m_renderContext.BeginFrame() == false) return false;
			// モデルの描画
			m_modelPass.Render(m_renderContext.GetCommandList(), m_renderContext.GetBackBufferIndex(), m_wolfModel, core::Matrix::RotationY(m_modelAngle), m_viewProjection);
			// デバッグ情報の描画
			m_debugOverlay.Render(m_renderContext.GetCommandList(), m_renderContext.GetCpuFrameMilliseconds(), m_renderContext.GetGpuFrameMilliseconds(), m_memory);
			// 描画の終了
			if (m_renderContext.EndFrame() == false) return false;
		}

		return true;
	}


	HWND Application::CreateMainWindow()
	{
		HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass{};
		windowClass.cbSize        = sizeof(WNDCLASSEXW);
		windowClass.lpfnWndProc   = WindowProcedure;
		windowClass.hInstance     = instance;
		windowClass.lpszClassName = WINDOW_CLASS_NAME;
		ATOM atom = RegisterClassExW(&windowClass);

		if (atom == 0) {
			core::LogError(L"ウィンドウクラスの登録に失敗しました。");
			return nullptr;
		}

		HWND window = CreateWindowExW(
			0,
			WINDOW_CLASS_NAME,
			WINDOW_TITLE,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			WINDOW_WIDTH,
			WINDOW_HEIGHT,
			nullptr,
			nullptr,
			instance,
			nullptr
		);

		if (window == nullptr) {
			core::LogError(L"ウィンドウの作成に失敗しました。");
			return nullptr;
		}

		ShowWindow(window, SW_SHOW);

		return window;
	}
} // namespace fangEngine
