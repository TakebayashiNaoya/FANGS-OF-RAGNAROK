/**
 * @file DebugOverlay.cpp
 * @brief デバッグ用オーバーレイ描画クラスの実装
 */
#include "DebugOverlay.h"
#include <iostream>
#include "RenderContext.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


namespace fangEngine::render
{
	namespace
	{
		constexpr UINT SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT = 64; // ImGuiのサンプルと同じ数。今使うのはフォントの分だけ。

		constexpr const char* FONT_PATH = "C:/Windows/Fonts/meiryo.ttc";
		constexpr float       FONT_SIZE = 18.0f;

		constexpr float MILLISECONDS_PER_SECOND = 1000.0f;

		/**
		 * @brief UTF-8の文字列をImGuiが受け取る型に変える
		 * @param text UTF-8の文字列
		 * @return ImGuiに渡せる文字列
		 */
		const char* ToImGuiText(const char8_t* text)
		{
			return reinterpret_cast<const char*>(text);
		}
	}


	DebugOverlay::~DebugOverlay()
	{
		// 初期化が終わっていない場合は、動いていないものを終わらせないため何もしない
		if (m_initialized == false) return;

		// 初期化の逆の順に終わらせる
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}


	bool DebugOverlay::Initialize(HWND window, RenderContext& renderContext)
	{
		// ディスクリプタヒープの作成
		if (CreateShaderResourceViewDescriptorHeap(renderContext.GetDevice()) == false) return false;

		// ImGui本体の初期化
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		// 日本語フォントの読み込み。失敗しても既定のフォントで動かす。
		ImFontConfig fontConfig{};
		fontConfig.Flags |= ImFontFlags_NoLoadError;
		ImGui::GetIO().Fonts->AddFontFromFileTTF(FONT_PATH, FONT_SIZE, &fontConfig);

		// Win32側の初期化
		if (ImGui_ImplWin32_Init(window) == false) {
			std::cerr << "ImGui_ImplWin32_Initに失敗しました。" << std::endl;
			return false;
		}

		// ImGui_ImplDX12_Initの引数に渡す構造体を作る
		ImGui_ImplDX12_InitInfo initInfo{};
		initInfo.Device               = renderContext.GetDevice();
		initInfo.CommandQueue         = renderContext.GetCommandQueue();
		initInfo.NumFramesInFlight    = RenderContext::RENDER_TARGET_COUNT;
		initInfo.RTVFormat            = RenderContext::RENDER_TARGET_FORMAT;
		initInfo.DSVFormat            = DXGI_FORMAT_UNKNOWN;
		initInfo.SrvDescriptorHeap    = m_shaderResourceViewDescriptorHeap.Get();
		initInfo.UserData             = this;
		initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
			{
				static_cast<DebugOverlay*>(info->UserData)->AllocateDescriptor(cpuHandle, gpuHandle);
			};
		initInfo.SrvDescriptorFreeFn  = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE)
			{
				static_cast<DebugOverlay*>(info->UserData)->FreeDescriptor(cpuHandle);
			};

		// D3D12側の初期化
		if (ImGui_ImplDX12_Init(&initInfo) == false) {
			std::cerr << "ImGui_ImplDX12_Initに失敗しました。" << std::endl;
			return false;
		}

		// 初期化が最後まで終わったらtrueにする
		m_initialized = true;

		return true;
	}


	void DebugOverlay::Render(ID3D12GraphicsCommandList* commandList)
	{
		// ImGuiのフレームを始める
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// メニューバーの中身を組み立てる
		const float framesPerSecond       = ImGui::GetIO().Framerate;
		const float frameTimeMilliseconds = MILLISECONDS_PER_SECOND / framesPerSecond;
		if (ImGui::BeginMainMenuBar()) {
			ImGui::Text("FPS: %.1f", framesPerSecond);
			ImGui::Text(ToImGuiText(u8"1フレーム: %.2f ms"), frameTimeMilliseconds);
			ImGui::EndMainMenuBar();
		}

		// 組み立てた内容を描画データにする
		ImGui::Render();

		// 描画データをコマンドリストに積む
		ID3D12DescriptorHeap* descriptorHeaps[] = { m_shaderResourceViewDescriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, descriptorHeaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	}


	bool DebugOverlay::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		return ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam) != 0;
	}


	bool DebugOverlay::CreateShaderResourceViewDescriptorHeap(ID3D12Device* device)
	{
		// シェーダーリソースビュー用のディスクリプタヒープの作成
		D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewDescriptorHeapDescription{};
		shaderResourceViewDescriptorHeapDescription.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		shaderResourceViewDescriptorHeapDescription.NumDescriptors = SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT;
		shaderResourceViewDescriptorHeapDescription.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		HRESULT createDescriptorHeapResult = device->CreateDescriptorHeap(
			&shaderResourceViewDescriptorHeapDescription,
			IID_PPV_ARGS(&m_shaderResourceViewDescriptorHeap)
		);
		if (FAILED(createDescriptorHeapResult)) {
			std::cerr << "シェーダーリソースビュー用のディスクリプタヒープの作成に失敗しました。" << std::endl;
			return false;
		}

		// ディスクリプタの増分サイズを取得
		m_descriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// 空いているディスクリプタの番号を初期化
		m_freeDescriptorIndices.reserve(SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT);
		for (UINT descriptorIndex = 0; descriptorIndex < SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT; descriptorIndex++) {
			m_freeDescriptorIndices.push_back(descriptorIndex);
		}

		return true;
	}


	void DebugOverlay::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
	{
		// 空いている番号を1つ取り出す
		UINT descriptorIndex = m_freeDescriptorIndices.back();
		m_freeDescriptorIndices.pop_back();
		// 番号をCPUとGPUのアドレスに変える
		cpuHandle->ptr = m_shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + descriptorIndex * m_descriptorHandleIncrementSize;
		gpuHandle->ptr = m_shaderResourceViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr + descriptorIndex * m_descriptorHandleIncrementSize;
	}


	void DebugOverlay::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
	{
		// アドレスを番号に戻して一覧へ返す(GPUでも同じ結果(番号)になるためCPUだけでOK)
		UINT descriptorIndex = static_cast<UINT>((cpuHandle.ptr - m_shaderResourceViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr) / m_descriptorHandleIncrementSize);
		m_freeDescriptorIndices.push_back(descriptorIndex);
	}
} // namespace fangEngine::render
