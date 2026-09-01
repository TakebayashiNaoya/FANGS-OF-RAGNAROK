/**
 * @file GameMain.cpp
 * @brief ゲームの起動処理。エディタを差し込む唯一の場所。
 */
#include "GameMain.h"
#include "Runtime/Application.h"

#if FANG_ENABLE_EDITOR
#include "Editor/EditorUI.h"
#endif


namespace fang::game
{
	namespace
	{
#if FANG_ENABLE_EDITOR

		using EditorUI = editor::EditorUI;

#else

		/**
		 * @brief エディタを外した構成で EditorUI の代わりに置くもの。
		 * @details 呼び出しが全部空になるので、ゲーム側の関数に #if を書かずに済む。
		 *          Release では Editor も imgui もリンクされない。
		 */
		class EditorUI
		{
		public:
			[[nodiscard]] bool Initialize(const EngineContext&, rhi::GraphicsDevice&, const Window&) { return true; }
			void               BuildFrame(const Window&, float) {}
			void               Render(rhi::GraphicsDevice&, rhi::CommandList&) {}
			void               Shutdown(rhi::GraphicsDevice&) {}
		};

#endif

		/**
		 * @brief ゲーム本体。Runtime のフレームループから呼ばれる。
		 * @details Game 側でエディタに触れるのはこのクラスの中だけ。
		 * @threading メインスレッドのみ。
		 */
		class FangsOfRagnarok final : public IApplication
		{
		public:
			[[nodiscard]] bool OnInitialize(
				const EngineContext& context,
				rhi::GraphicsDevice& device,
				const Window&        window
			) override
			{
				return m_editorUI.Initialize(context, device, window);
			}

			void OnUpdate(const Window& window, float deltaTimeSeconds) override
			{
				// TODO: 狼・オーディン・昼夜の更新を書く（Phase 6）。
				m_editorUI.BuildFrame(window, deltaTimeSeconds);
			}

			void OnRender(rhi::GraphicsDevice& device, rhi::CommandList& commandList) override
			{
				m_editorUI.Render(device, commandList);
			}

			void OnShutdown(rhi::GraphicsDevice& device) override { m_editorUI.Shutdown(device); }


		private:
			EditorUI m_editorUI; /**< エディタ UI。Release 構成では空の代役に差し替わる。 */
		};
	} // namespace

	int Run()
	{
		FangsOfRagnarok application;
		return RunApplication(application);
	}
} // namespace fang::game
