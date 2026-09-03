/**
 * @file GameMain.cpp
 * @brief ゲームの起動処理。エディタを差し込む唯一の場所。
 */
#include "GameMain.h"
#include "Core/Memory/FrameAllocator.h"
#include "Runtime/Application.h"
#include <cmath>

#if FANG_ENABLE_EDITOR
#include "Editor/EditorUI.h"
#endif


namespace fang::game
{
	namespace
	{
		/** @brief 円周率。Core/Math がまだ定数を持っていないのでここに置く。 */
		constexpr float PI = 3.14159265f;

		/** @brief 光源が狼の周りを 1 周する秒数。 */
		constexpr float LIGHT_ORBIT_SECONDS = 10.0f;

		// 既定の光（DirectionalLight の初期値）と同じ仰角を保ち、方位だけを回す。2 つの値は既定の向き
		// (0.309, 0.722, -0.619) の水平成分の長さと高さで、正規化済みの値から取っているので回しても長さ 1 のまま。
		constexpr float LIGHT_DIRECTION_HORIZONTAL = 0.692f;
		constexpr float LIGHT_DIRECTION_HEIGHT     = 0.722f;
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
			void               BuildFrame(const Window&, float, const RenderStatistics&) {}
			void               Render(rhi::GraphicsDevice&, rhi::CommandList&) {}
			void               RunRequestedTestLoad(uint64_t) {}
			void               Shutdown(rhi::GraphicsDevice&) {}
		};

#endif


		/**
		 * @brief ゲーム本体。Runtime のフレームループから呼ばれる。
		 * @details Game 側でエディタに触れるのはこのクラスの中だけ。
		 * @threading メインスレッドのみ。ただし OnUpdate はワーカースレッドで走り、
		 *            m_lightOrbitRadians はそこからしか触らない。
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

			[[nodiscard]] FrameData* OnUpdate(const FrameUpdateContext& context) override
			{
				// TODO: 狼・オーディン・昼夜の更新を書く。
				m_editorUI.RunRequestedTestLoad(context.frameIndex);

				// 昼夜サイクルはまだ無いので、光の向きを時間で回して「毎フレーム渡せる」ことを目で確かめる。
				m_lightOrbitRadians += context.deltaTimeSeconds * (2.0f * PI / LIGHT_ORBIT_SECONDS);
				if (m_lightOrbitRadians >= 2.0f * PI)
				{
					// 積みっぱなしにすると値が大きくなるほど角度の刻みが粗くなるので、1 周ごとに戻す。
					m_lightOrbitRadians -= 2.0f * PI;
				}

				FrameData* frameData = NewFrame<FrameData>(context.frameAllocator);
				if (frameData == nullptr)
				{
					// フレームメモリが足りないだけなら落とさない。このフレームは既定の光で描かれる。
					return nullptr;
				}

				frameData->light.directionToLight = {
					std::sinf(m_lightOrbitRadians) * LIGHT_DIRECTION_HORIZONTAL,
					LIGHT_DIRECTION_HEIGHT,
					std::cosf(m_lightOrbitRadians) * LIGHT_DIRECTION_HORIZONTAL,
				};

				return frameData;
			}

			void OnRender(const FrameRenderContext& context) override
			{
				// ImGui は NewFrame と Render を同じフレームで対にしないといけないので、組み立てもここで行う。
				m_editorUI.BuildFrame(context.window, context.deltaTimeSeconds, context.renderStatistics);
				m_editorUI.Render(context.device, context.commandList);
			}

			void OnShutdown(rhi::GraphicsDevice& device) override { m_editorUI.Shutdown(device); }


		private:
			EditorUI m_editorUI; /**< エディタ UI。Release 構成では空の代役に差し替わる。 */

			float m_lightOrbitRadians = 0.0f; /**< 光源の方位角。OnUpdate（ワーカースレッド）だけが触る。 */
		};
	} // namespace


	int Run()
	{
		FangsOfRagnarok application;
		return RunApplication(application);
	}
} // namespace fang::game
