/**
 * @file EditorUI.cpp
 * @brief ImGui エディタ本体と、RHI 上に自作した描画バックエンド。
 */
#include "Pch.h"
#include "Editor/EditorUI.h"
#include "Core/Log/Assert.h"
#include "Core/Platform/AssetPath.h"
#include "Core/Platform/Window.h"
#include "Core/Text/MissingGlyphCounter.h"
#include "Editor/EditorLog.h"
#include "Editor/ImGuiPlatformInput.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Runtime/EngineContext.h"
#include "Runtime/FrameContext.h"
#include "Runtime/FramePipeline.h"


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

		/** @brief 同梱フォント。PC と実機で同じ字が出るよう、OS のフォントは使わない。 */
		constexpr const char* FONT_RELATIVE_PATH = "Fonts\\MPLUS1p-Regular.ttf";

		/** @brief アトラスの上限（RGBA32、1024x2048）。フォントを差し替えて黙って倍にならないための歯止め。 */
		constexpr size_t MAXIMUM_FONT_ATLAS_BYTES = 8 * 1024 * 1024;

		/**
		 * @brief 積む字の範囲。範囲表を持たない。
		 * @details ImGui は cmap に無いコードポイントを黙って飛ばすので、BMP 全域を要求すると
		 *          「フォントが持つ字を全部」と同じ意味になる（要求 = 実在）。
		 */
		constexpr ImWchar GLYPH_RANGES[] = { 0x0020, 0xFFFF, 0 };

		/** @brief 「エンジン情報」ウィンドウの最小幅。 */
		constexpr float ENGINE_INFO_WINDOW_MIN_WIDTH = 360.0f;

#if FANG_DEBUG
		constexpr const char* CONFIGURATION_DISPLAY_NAME = "Debug";
#elif FANG_PREVIEW
		constexpr const char* CONFIGURATION_DISPLAY_NAME = "Preview";
#else
		constexpr const char* CONFIGURATION_DISPLAY_NAME = "Release";
#endif

		/** @brief 同梱フォントを読み込む。無ければ ImGui の既定フォント（英数字のみ）のままにする。 */
		void LoadBundledFont()
		{
			ImGuiIO& io = ImGui::GetIO();

			const std::string fontPath = MakeAssetPath(FONT_RELATIVE_PATH);
			if (fontPath.empty())
			{
				FANG_LOG_WARNING(Editor, "アセットの根っこが見つからないので既定フォントを使う");
				return;
			}

			ImFontConfig fontConfig;
			// オーバーサンプリングを 1 に倒す。字数を BMP 全域へ増やしてもアトラスの大きさが変わらないため。
			fontConfig.PixelSnapH = true;

			if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), FONT_SIZE_IN_PIXELS, &fontConfig, GLYPH_RANGES) ==
				nullptr)
			{
				FANG_LOG_WARNING(Editor, "フォントを読めなかった: {}", fontPath);
				return;
			}

			FANG_LOG_INFO(Editor, "フォントを読んだ: {}", fontPath);
		}


		/** @brief 欠字カウンタが欠字を見つけたときの通知先。ImGui を 1 つも知らない Core 側から呼ばれる。 */
		void ReportMissingGlyph(char32_t codePoint, void* /*userData*/)
		{
			FANG_LOG_WARNING(Editor, "欠字 U+{:04X}", static_cast<uint32_t>(codePoint));
		}


		/**
		 * @brief 欠字カウンタの通知先を差す前に、フックが生きているかを自己診断する。
		 * @details U+FFFF はどのフォントにも無い非文字。これを 1 つ引いて種類数が 1 になるかで、
		 *          imgui を上流に入れ替えたときにフックが消えていないかを確かめる（ADR-053）。
		 *          診断の 1 字が「欠字 0 件」の判定を汚さないよう、Reset の後で通知先を差す。
		 */
		void InitializeMissingGlyphDetection()
		{
			ImFontAtlas* fontAtlas = ImGui::GetIO().Fonts;
			FANG_ASSERT(fontAtlas->Fonts.Size > 0, "フォントアトラスにフォントが無い");
			ImFont* font = fontAtlas->Fonts[0];

			MissingGlyphCounter& counter = MissingGlyphCounter::GetInstance();

			counter.Reset();
			(void)font->FindGlyph(0xFFFF);
			if (counter.GetDistinctMissingCount() != 1)
			{
				FANG_LOG_ERROR(Editor, "欠字の検出フックが効いていない");
			}

			counter.Reset();
			counter.SetCallback(&ReportMissingGlyph, nullptr);
		}
	} // namespace


	EditorUI::~EditorUI()
	{
		FANG_ASSERT(!m_isInitialized, "EditorUI::Shutdown が呼ばれていない");
	}


	bool EditorUI::Initialize(const EngineContext& context, rhi::GraphicsDevice& device, const Window& window)
	{
		FANG_ASSERT(!m_isInitialized, "EditorUI を二重に初期化している");

		IMGUI_CHECKVERSION();
		if (ImGui::CreateContext() == nullptr)
		{
			FANG_LOG_ERROR(Editor, "ImGui のコンテキストを作れなかった");
			return false;
		}

		// 以降で失敗しても、作ったコンテキストは Shutdown が壊す。
		m_isInitialized = true;

		m_framePipeline = &context.framePipeline;

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// 実機はマウスが無いのでパッドのナビゲーションが唯一の操作手段になる。
		// 立てるだけでは何も起きず、実際に効くのは HasGamepad を立てるプラットフォームだけ。
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

		io.BackendRendererName = "FangEngine RHI";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));

		LoadBundledFont();
		ImGui::StyleColorsDark();

		if (!InitializeBackend(device))
		{
			FANG_LOG_ERROR(Editor, "ImGui の描画バックエンドを作れなかった");
			return false;
		}

		InitializeMissingGlyphDetection();

		if (!m_budgetPanel.Initialize(context))
		{
			FANG_LOG_ERROR(Editor, "Xbox 予算のパネルを作れなかった");
			return false;
		}

		if (!m_jobSystemPanel.Initialize(context))
		{
			FANG_LOG_ERROR(Editor, "ジョブシステムのパネルを作れなかった");
			return false;
		}

		if (!m_renderStatisticsPanel.Initialize())
		{
			FANG_LOG_ERROR(Editor, "レンダリング統計のパネルを作れなかった");
			return false;
		}

		if (!m_tuningPanel.Initialize())
		{
			FANG_LOG_ERROR(Editor, "調整値のパネルを作れなかった");
			return false;
		}

#if FANG_ENABLE_HOT_RELOAD
		if (!m_shaderReloadPanel.Initialize(context.shaderReloadStatus))
		{
			FANG_LOG_ERROR(Editor, "シェーダーホットリロードのパネルを作れなかった");
			return false;
		}
#endif

		FANG_LOG_INFO(Editor, "エディタ UI を初期化した");

		return true;
	}


	void EditorUI::Shutdown(rhi::GraphicsDevice& device)
	{
		if (!m_isInitialized)
		{
			return;
		}

#if FANG_ENABLE_HOT_RELOAD
		m_shaderReloadPanel.Shutdown();
#endif

		m_tuningPanel.Shutdown();
		m_renderStatisticsPanel.Shutdown();
		m_jobSystemPanel.Shutdown();
		m_budgetPanel.Shutdown();
		ShutdownBackend(device);

		m_framePipeline = nullptr;
		m_isInitialized = false;

		ImGui::DestroyContext();
	}


	void EditorUI::BuildFrame(const Window& window, float deltaTimeSeconds, const RenderStatistics& renderStatistics)
	{
		FANG_ASSERT(m_isInitialized, "EditorUI が初期化されていない");

		ImGuiIO& io    = ImGui::GetIO();
		io.DisplaySize = ImVec2(static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()));
		io.DeltaTime   = deltaTimeSeconds > 0.0f ? deltaTimeSeconds : 1.0f / 60.0f;

		UpdateImGuiPlatformInput(window.GetNativeHandle());

		ImGui::NewFrame();

		BuildEngineInfoWindow(window, deltaTimeSeconds);
		m_jobSystemPanel.BuildFrame(deltaTimeSeconds, m_framePipeline->GetFrameIndex());
		m_renderStatisticsPanel.BuildFrame(deltaTimeSeconds, renderStatistics);
		m_budgetPanel.BuildFrame();
		m_tuningPanel.BuildFrame();

#if FANG_ENABLE_HOT_RELOAD
		m_shaderReloadPanel.BuildFrame();
#endif

		// ImGui 付属のデモ。中身は英語なので既定では出さない。
		if (m_isDemoWindowVisible)
		{
			ImGui::ShowDemoWindow(&m_isDemoWindowVisible);
		}
	}


	void EditorUI::BuildEngineInfoWindow(const Window& window, float deltaTimeSeconds)
	{
		// TODO: ヒエラルキー・インスペクタ・コンソールに置き換える。
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

#if FANG_ENABLE_PROFILER
		// 1 周が更新と描画の合計より短ければ、その差だけ 2 つが重なって回っている。
		ImGui::Text("更新: %.2f ms（ジョブ）", m_framePipeline->GetUpdateMilliseconds());
		ImGui::Text("描画: %.2f ms（メイン）", m_framePipeline->GetRenderMilliseconds());
		ImGui::Text("1 周: %.2f ms", m_framePipeline->GetFrameMilliseconds());
#endif

		ImGui::Separator();

		// FIXME: Checkbox のラベル末尾が全角の「）」だとその 1 文字だけ描かれない。ImGui::Text では出る。
		ImGui::Checkbox("ImGui のデモを表示", &m_isDemoWindowVisible);

		ImGui::End();
	}


	void EditorUI::Render(rhi::GraphicsDevice& device, rhi::CommandList& commandList)
	{
		FANG_ASSERT(m_isInitialized, "EditorUI が初期化されていない");

		ImGui::Render();
		RenderDrawData(device, commandList, *ImGui::GetDrawData());
	}


	void EditorUI::RunRequestedTestLoad(uint64_t frameIndex)
	{
		m_jobSystemPanel.RunRequestedTestLoad(frameIndex);
	}


	bool EditorUI::InitializeBackend(rhi::GraphicsDevice& device)
	{
		constexpr rhi::VertexAttribute VERTEX_LAYOUT[] = {
			{ "POSITION", 0, rhi::EnVertexFormat::Float2, offsetof(ImDrawVert, pos) },
			{ "TEXCOORD", 0, rhi::EnVertexFormat::Float2, offsetof(ImDrawVert, uv) },
			{ "COLOR", 0, rhi::EnVertexFormat::UByte4Normalized, offsetof(ImDrawVert, col) },
		};

		// シェーダーは Shaders/*.hlsl をビルド時に FXC でヘッダ化したもの。UWP に実行時コンパイルが無いため。
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShader = rhi::MakeShaderSource(
			std::span<const uint8_t>(g_ImGuiVS, sizeof(g_ImGuiVS)),
			"Engine/Editor/Shaders/ImGuiVS.hlsl",
			"VertexMain"
		);
		pipelineDesc.pixelShader = rhi::MakeShaderSource(
			std::span<const uint8_t>(g_ImGuiPS, sizeof(g_ImGuiPS)),
			"Engine/Editor/Shaders/ImGuiPS.hlsl",
			"PixelMain"
		);
		pipelineDesc.vertexLayout        = VERTEX_LAYOUT;
		pipelineDesc.rootConstantCount   = 16;
		pipelineDesc.textureCount        = 1;
		pipelineDesc.isAlphaBlendEnabled = true;

		m_pipeline = device.CreateGraphicsPipeline(pipelineDesc);
		if (!m_pipeline.IsValid())
		{
			return false;
		}

		ImFontAtlas*   fontAtlas   = ImGui::GetIO().Fonts;
		unsigned char* pixels      = nullptr;
		int            atlasWidth  = 0;
		int            atlasHeight = 0;
		fontAtlas->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);

		const size_t atlasBytes = static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4;
		const int    glyphCount = fontAtlas->Fonts.Size > 0 ? fontAtlas->Fonts[0]->Glyphs.Size : 0;
		FANG_LOG_INFO(
			Editor,
			"フォントアトラス: {}x{}（{:.2f} MiB, {} 字）",
			atlasWidth,
			atlasHeight,
			static_cast<double>(atlasBytes) / (1024.0 * 1024.0),
			glyphCount
		);
		FANG_ASSERT(atlasBytes <= MAXIMUM_FONT_ATLAS_BYTES, "フォントアトラスが上限を超えた");

		m_fontTexture =
			device.CreateTexture2D(pixels, static_cast<uint32_t>(atlasWidth), static_cast<uint32_t>(atlasHeight));

		if (!m_fontTexture.IsValid())
		{
			return false;
		}

		// ImGui は焼いた画素を CPU 側に持ち続ける（RGBA32 + Alpha8 で計 10 MiB）が、
		// アトラスは二度と組み直さないので要らない。
		fontAtlas->ClearTexData();

		return true;
	}


	void EditorUI::ShutdownBackend(rhi::GraphicsDevice& device)
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


	void EditorUI::RenderDrawData(
		rhi::GraphicsDevice& device,
		rhi::CommandList&    commandList,
		const ImDrawData&    drawData
	)
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
		commandList.SetViewport(
			static_cast<uint32_t>(drawData.DisplaySize.x),
			static_cast<uint32_t>(drawData.DisplaySize.y)
		);

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
				commandList.DrawIndexed(
					drawCommand.ElemCount,
					drawCommand.IdxOffset + globalIndexOffset,
					static_cast<int32_t>(drawCommand.VtxOffset) + globalVertexOffset
				);
			}

			globalIndexOffset += static_cast<uint32_t>(drawList.IdxBuffer.Size);
			globalVertexOffset += drawList.VtxBuffer.Size;
		}
	}


	bool EditorUI::EnsureBufferCapacity(rhi::GraphicsDevice& device, const ImDrawData& drawData)
	{
		const uint32_t requiredVertexCount = static_cast<uint32_t>(drawData.TotalVtxCount);
		const uint32_t requiredIndexCount  = static_cast<uint32_t>(drawData.TotalIdxCount);

		if (requiredVertexCount > m_vertexCapacity)
		{
			// 前フレームの描画は EndFrame で待ち終わっているので、その場で作り直してよい。
			device.DestroyBuffer(m_vertexBuffer);
			m_vertexCapacity = requiredVertexCount + BUFFER_GROWTH_MARGIN;
			m_vertexBuffer   = device.CreateDynamicBuffer(
				m_vertexCapacity * static_cast<uint32_t>(sizeof(ImDrawVert)),
				static_cast<uint32_t>(sizeof(ImDrawVert)),
				rhi::EnBufferKind::Vertex
			);
		}

		if (requiredIndexCount > m_indexCapacity)
		{
			device.DestroyBuffer(m_indexBuffer);
			m_indexCapacity = requiredIndexCount + BUFFER_GROWTH_MARGIN;
			m_indexBuffer   = device.CreateDynamicBuffer(
				m_indexCapacity * static_cast<uint32_t>(sizeof(ImDrawIdx)),
				static_cast<uint32_t>(sizeof(ImDrawIdx)),
				rhi::EnBufferKind::Index
			);
		}

		return m_vertexBuffer.IsValid() && m_indexBuffer.IsValid();
	}


	void EditorUI::CopyDrawData(rhi::GraphicsDevice& device, const ImDrawData& drawData)
	{
		m_vertexStaging.clear();
		m_indexStaging.clear();
		m_vertexStaging.reserve(static_cast<size_t>(drawData.TotalVtxCount));
		m_indexStaging.reserve(static_cast<size_t>(drawData.TotalIdxCount));

		for (int listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex)
		{
			const ImDrawList& drawList = *drawData.CmdLists[listIndex];
			m_vertexStaging.insert(
				m_vertexStaging.end(),
				drawList.VtxBuffer.Data,
				drawList.VtxBuffer.Data + drawList.VtxBuffer.Size
			);
			m_indexStaging.insert(
				m_indexStaging.end(),
				drawList.IdxBuffer.Data,
				drawList.IdxBuffer.Data + drawList.IdxBuffer.Size
			);
		}

		device.UpdateBuffer(
			m_vertexBuffer,
			m_vertexStaging.data(),
			static_cast<uint32_t>(m_vertexStaging.size() * sizeof(ImDrawVert))
		);
		device.UpdateBuffer(
			m_indexBuffer,
			m_indexStaging.data(),
			static_cast<uint32_t>(m_indexStaging.size() * sizeof(ImDrawIdx))
		);
	}
} // namespace fang::editor
