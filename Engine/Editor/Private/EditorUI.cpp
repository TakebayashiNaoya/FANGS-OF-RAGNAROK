/**
 * @file EditorUI.cpp
 * @brief ImGui エディタ本体と、RHI 上に自作した描画バックエンド。
 */
#include "Pch.h"
#include "Editor/EditorUI.h"
#include "Core/Log/Assert.h"
#include "Core/Memory/Allocator.h"
#include "Core/Platform/SystemFont.h"
#include "Core/Platform/Window.h"
#include "Editor/EditorLog.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "ImGuiPlatformInput.h"
#include <imgui.h>
#include <vector>


FANG_DEFINE_LOG_CATEGORY(Editor);


// FXC の /Fh が吐くヘッダは BYTE 型の配列なので、<windows.h> を入れずに済むよう自前で合わせる。
using BYTE = unsigned char;
#include "ImGuiPS.h"
#include "ImGuiVS.h"


namespace fang::editor
{
	namespace
	{
		/** @brief 頂点とインデックスのバッファを作り直すときの余裕。 */
		constexpr uint32_t BUFFER_GROWTH_MARGIN = 5000;

		/** @brief エディタのフォントサイズ。 */
		constexpr float FONT_SIZE_IN_PIXELS = 18.0f;

		/** @brief 「エンジン情報」ウィンドウの最小幅。 */
		constexpr float ENGINE_INFO_WINDOW_MIN_WIDTH = 360.0f;

#if FANG_DEBUG
		constexpr const char* CONFIGURATION_DISPLAY_NAME = "Debug";
#elif FANG_PREVIEW
		constexpr const char* CONFIGURATION_DISPLAY_NAME = "Preview";
#else
		constexpr const char* CONFIGURATION_DISPLAY_NAME = "Release";
#endif

		/** @brief 日本語が出るフォントを読み込む。無ければ ImGui の既定フォント（英数字のみ）のままにする。 */
		void LoadJapaneseFont()
		{
			ImGuiIO& io = ImGui::GetIO();

			const std::string fontPath = GetSystemUIFontPath();
			if (fontPath.empty())
			{
				FANG_LOG_WARNING(Editor, "日本語フォントが見つからないので既定フォントを使う");
				return;
			}

			ImFontConfig fontConfig;
			// .ttc は複数のフォントの束なので、先頭（レギュラー）を指定する。
			fontConfig.FontNo = 0;

			if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(),
											 FONT_SIZE_IN_PIXELS,
											 &fontConfig,
											 io.Fonts->GetGlyphRangesJapanese()) == nullptr)
			{
				FANG_LOG_WARNING(Editor, "フォントを読めなかった: {}", fontPath);
				return;
			}

			FANG_LOG_INFO(Editor, "フォントを読んだ: {}", fontPath);
		}
	} // namespace

	/**
	 * @brief ImGui の描画データを RHI に流すバックエンド。
	 * @details imgui_impl_dx12 は使わず、RHI の公開 API だけで描く。
	 * @threading メインスレッドのみ。
	 */
	class EditorUI::Backend
	{
	public:
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device)
		{
			constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
				{ "POSITION", 0, rhi::EnVertexFormat::Float2, offsetof(ImDrawVert, pos) },
				{ "TEXCOORD", 0, rhi::EnVertexFormat::Float2, offsetof(ImDrawVert, uv) },
				{ "COLOR", 0, rhi::EnVertexFormat::UByte4Normalized, offsetof(ImDrawVert, col) },
			};

			// シェーダーは Shaders/*.hlsl をビルド時に FXC でヘッダ化したもの。UWP に実行時コンパイルが無いため。
			rhi::GraphicsPipelineDesc pipelineDesc{};
			pipelineDesc.vertexShaderBytecode = std::span<const uint8_t>(g_ImGuiVS, sizeof(g_ImGuiVS));
			pipelineDesc.pixelShaderBytecode  = std::span<const uint8_t>(g_ImGuiPS, sizeof(g_ImGuiPS));
			pipelineDesc.vertexLayout         = VERTEX_LAYOUT;
			pipelineDesc.rootConstantCount    = 16;
			pipelineDesc.hasTexture           = true;
			pipelineDesc.isAlphaBlendEnabled  = true;

			m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
			if (!m_pipeline.IsValid())
			{
				return false;
			}

			unsigned char* pixels      = nullptr;
			int            atlasWidth  = 0;
			int            atlasHeight = 0;
			ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);

			m_fontTexture =
				device.CreateTexture2D(pixels, static_cast<uint32_t>(atlasWidth), static_cast<uint32_t>(atlasHeight));

			return m_fontTexture.IsValid();
		}

		void Shutdown(rhi::GraphicsDevice& device)
		{
			device.DestroyBuffer(m_indexBuffer);
			device.DestroyBuffer(m_vertexBuffer);
			device.DestroyTexture(m_fontTexture);
			device.DestroyPipeline(m_pipeline);

			m_indexBuffer    = {};
			m_vertexBuffer   = {};
			m_fontTexture    = {};
			m_pipeline       = {};
			m_vertexCapacity = 0;
			m_indexCapacity  = 0;
		}

		void Render(rhi::GraphicsDevice& device, rhi::CommandList& commandList, const ImDrawData& drawData)
		{
			if (drawData.TotalVtxCount <= 0)
			{
				return;
			}

			if (!EnsureBufferCapacity(device, drawData))
			{
				return;
			}

			CopyDrawData(device, drawData);

			const float left   = drawData.DisplayPos.x;
			const float right  = drawData.DisplayPos.x + drawData.DisplaySize.x;
			const float top    = drawData.DisplayPos.y;
			const float bottom = drawData.DisplayPos.y + drawData.DisplaySize.y;

			// HLSL の定数バッファは既定で列優先に詰められるので、転置した並びで渡す。
			// clang-format off
			const float projectionMatrix[16] = {
				2.0f / (right - left), 0.0f,                  0.0f, 0.0f,
				0.0f,                  2.0f / (top - bottom), 0.0f, 0.0f,
				0.0f,                  0.0f,                  0.5f, 0.0f,
				(right + left) / (left - right), (top + bottom) / (bottom - top), 0.5f, 1.0f,
			};
			// clang-format on

			commandList.SetPipeline(m_pipeline);
			commandList.SetVertexBuffer(m_vertexBuffer);
			commandList.SetIndexBuffer(m_indexBuffer);
			commandList.SetRootConstants(projectionMatrix, 16);
			commandList.SetViewport(static_cast<uint32_t>(drawData.DisplaySize.x),
									static_cast<uint32_t>(drawData.DisplaySize.y));

			// TODO: ImTextureID からハンドルを引く。今はフォントアトラスしか無い。
			commandList.SetTexture(m_fontTexture);

			uint32_t globalIndexOffset  = 0;
			int32_t  globalVertexOffset = 0;
			for (int listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex)
			{
				const ImDrawList& drawList = *drawData.CmdLists[listIndex];
				for (int commandIndex = 0; commandIndex < drawList.CmdBuffer.Size; ++commandIndex)
				{
					const ImDrawCmd& drawCommand = drawList.CmdBuffer[commandIndex];
					if (drawCommand.UserCallback != nullptr)
					{
						continue;
					}

					const int32_t clipLeft   = static_cast<int32_t>(drawCommand.ClipRect.x - drawData.DisplayPos.x);
					const int32_t clipTop    = static_cast<int32_t>(drawCommand.ClipRect.y - drawData.DisplayPos.y);
					const int32_t clipRight  = static_cast<int32_t>(drawCommand.ClipRect.z - drawData.DisplayPos.x);
					const int32_t clipBottom = static_cast<int32_t>(drawCommand.ClipRect.w - drawData.DisplayPos.y);
					if (clipRight <= clipLeft || clipBottom <= clipTop)
					{
						continue;
					}

					commandList.SetScissor(clipLeft, clipTop, clipRight, clipBottom);
					commandList.DrawIndexed(drawCommand.ElemCount,
											drawCommand.IdxOffset + globalIndexOffset,
											static_cast<int32_t>(drawCommand.VtxOffset) + globalVertexOffset);
				}

				globalIndexOffset += static_cast<uint32_t>(drawList.IdxBuffer.Size);
				globalVertexOffset += drawList.VtxBuffer.Size;
			}
		}


	private:
		[[nodiscard]] bool EnsureBufferCapacity(rhi::GraphicsDevice& device, const ImDrawData& drawData)
		{
			const uint32_t requiredVertexCount = static_cast<uint32_t>(drawData.TotalVtxCount);
			const uint32_t requiredIndexCount  = static_cast<uint32_t>(drawData.TotalIdxCount);

			if (requiredVertexCount > m_vertexCapacity)
			{
				// 前フレームの描画は EndFrame で待ち終わっているので、その場で作り直してよい。
				device.DestroyBuffer(m_vertexBuffer);
				m_vertexCapacity = requiredVertexCount + BUFFER_GROWTH_MARGIN;
				m_vertexBuffer =
					device.CreateDynamicBuffer(m_vertexCapacity * static_cast<uint32_t>(sizeof(ImDrawVert)),
											   static_cast<uint32_t>(sizeof(ImDrawVert)),
											   rhi::EnBufferKind::Vertex);
			}

			if (requiredIndexCount > m_indexCapacity)
			{
				device.DestroyBuffer(m_indexBuffer);
				m_indexCapacity = requiredIndexCount + BUFFER_GROWTH_MARGIN;
				m_indexBuffer   = device.CreateDynamicBuffer(m_indexCapacity * static_cast<uint32_t>(sizeof(ImDrawIdx)),
															 static_cast<uint32_t>(sizeof(ImDrawIdx)),
															 rhi::EnBufferKind::Index);
			}

			return m_vertexBuffer.IsValid() && m_indexBuffer.IsValid();
		}

		void CopyDrawData(rhi::GraphicsDevice& device, const ImDrawData& drawData)
		{
			m_vertexStaging.clear();
			m_indexStaging.clear();
			m_vertexStaging.reserve(static_cast<size_t>(drawData.TotalVtxCount));
			m_indexStaging.reserve(static_cast<size_t>(drawData.TotalIdxCount));

			for (int listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex)
			{
				const ImDrawList& drawList = *drawData.CmdLists[listIndex];
				m_vertexStaging.insert(m_vertexStaging.end(),
									   drawList.VtxBuffer.Data,
									   drawList.VtxBuffer.Data + drawList.VtxBuffer.Size);
				m_indexStaging.insert(m_indexStaging.end(),
									  drawList.IdxBuffer.Data,
									  drawList.IdxBuffer.Data + drawList.IdxBuffer.Size);
			}

			device.UpdateBuffer(m_vertexBuffer,
								m_vertexStaging.data(),
								static_cast<uint32_t>(m_vertexStaging.size() * sizeof(ImDrawVert)));
			device.UpdateBuffer(m_indexBuffer,
								m_indexStaging.data(),
								static_cast<uint32_t>(m_indexStaging.size() * sizeof(ImDrawIdx)));
		}

		rhi::PipelineHandle m_pipeline;           /**< ImGui 描画用のパイプライン。 */
		rhi::TextureHandle  m_fontTexture;        /**< フォントアトラス。ImGui が焼いたビットマップの転送先。 */
		rhi::BufferHandle   m_vertexBuffer;       /**< 毎フレーム書き換える動的頂点バッファ。 */
		rhi::BufferHandle   m_indexBuffer;        /**< 毎フレーム書き換える動的インデックスバッファ。 */
		uint32_t            m_vertexCapacity = 0; /**< 今のバッファに入る頂点数。足りなくなったら作り直す。 */
		uint32_t            m_indexCapacity  = 0; /**< 今のバッファに入るインデックス数。足りなくなったら作り直す。 */

		// TODO: フレームアロケータができたら差し替える（Phase 2）。
		std::vector<ImDrawVert> m_vertexStaging; /**< 全描画リストの頂点をまとめて 1 回で転送するための作業領域。 */
		std::vector<ImDrawIdx>  m_indexStaging;  /**< 同上のインデックス版。 */
	};

	/***************************************************************************************************/

	EditorUI::~EditorUI()
	{
		FANG_ASSERT(m_backend == nullptr, "EditorUI::Shutdown が呼ばれていない");
	}

	bool EditorUI::Initialize(rhi::GraphicsDevice& device, const Window& window)
	{
		FANG_ASSERT(m_backend == nullptr, "EditorUI を二重に初期化している");

		IMGUI_CHECKVERSION();
		if (ImGui::CreateContext() == nullptr)
		{
			FANG_LOG_ERROR(Editor, "ImGui のコンテキストを作れなかった");
			return false;
		}

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.BackendRendererName = "FangEngine RHI";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));

		LoadJapaneseFont();
		ImGui::StyleColorsDark();

		m_backend = New<Backend>(HeapAllocator::GetInstance());
		if (m_backend == nullptr || !m_backend->Initialize(device))
		{
			FANG_LOG_ERROR(Editor, "ImGui の描画バックエンドを作れなかった");
			return false;
		}

		FANG_LOG_INFO(Editor, "エディタ UI を初期化した");

		return true;
	}

	void EditorUI::Shutdown(rhi::GraphicsDevice& device)
	{
		if (m_backend == nullptr)
		{
			return;
		}

		m_backend->Shutdown(device);
		Delete(HeapAllocator::GetInstance(), m_backend);
		m_backend = nullptr;

		ImGui::DestroyContext();
	}

	void EditorUI::BuildFrame(const Window& window, float deltaTimeSeconds)
	{
		FANG_ASSERT(m_backend != nullptr, "EditorUI が初期化されていない");

		ImGuiIO& io    = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));
		io.DeltaTime   = deltaTimeSeconds > 0.0f ? deltaTimeSeconds : 1.0f / 60.0f;

		UpdateImGuiPlatformInput(window.GetNativeHandle());

		ImGui::NewFrame();

		BuildEngineInfoWindow(window, deltaTimeSeconds);

		// ImGui 付属のデモ。中身は英語なので既定では出さない。
		if (m_isDemoWindowVisible)
		{
			ImGui::ShowDemoWindow(&m_isDemoWindowVisible);
		}
	}

	void EditorUI::BuildEngineInfoWindow(const Window& window, float deltaTimeSeconds)
	{
		// TODO: Phase 2 以降、ヒエラルキー・インスペクタ・コンソールに置き換える。
		// 自動サイズだけだと日本語ラベルの末尾が切れるので下限を決めておく。
		ImGui::SetNextWindowSizeConstraints(ImVec2(ENGINE_INFO_WINDOW_MIN_WIDTH, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin("エンジン情報", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("構成: %s", CONFIGURATION_DISPLAY_NAME);
		ImGui::Text("ウィンドウ: %u x %u", window.GetWidth(), window.GetHeight());
		ImGui::Separator();

		const float frameTimeInMilliseconds = deltaTimeSeconds * 1000.0f;
		ImGui::Text("フレーム時間: %.2f ms", frameTimeInMilliseconds);
		ImGui::Text("フレームレート: %.0f fps", deltaTimeSeconds > 0.0f ? 1.0f / deltaTimeSeconds : 0.0f);
		ImGui::Separator();

		// FIXME: Checkbox のラベル末尾が全角の「）」だとその 1 文字だけ描かれない。ImGui::Text では出る。
		ImGui::Checkbox("ImGui のデモを表示", &m_isDemoWindowVisible);

		ImGui::End();
	}

	void EditorUI::Render(rhi::GraphicsDevice& device, rhi::CommandList& commandList)
	{
		FANG_ASSERT(m_backend != nullptr, "EditorUI が初期化されていない");

		ImGui::Render();
		m_backend->Render(device, commandList, *ImGui::GetDrawData());
	}
} // namespace fang::editor
