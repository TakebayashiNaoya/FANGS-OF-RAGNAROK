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


namespace fangEngine::render
{
	namespace
	{
		constexpr UINT SHADER_RESOURCE_VIEW_DESCRIPTOR_COUNT = 64; // ImGuiのサンプルと同じ数。今使うのはフォントの分だけ。

		constexpr const char* FONT_PATH = "C:/Windows/Fonts/meiryo.ttc";
		constexpr float       FONT_SIZE = 18.0f;
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

		return true;
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
