/**
 * @file GameMain.cpp
 * @brief ゲームの起動処理。エディタを差し込む唯一の場所。
 */
#include "GameMain.h"
#include "Collision/CollisionWorld.h"
#include "Core/Math/MathConstants.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "RHI/GraphicsDevice.h"
#include "Runtime/Application.h"
#include "Scene/Scene.h"
#include "GameLog.h"
#include "Wolf.h"
#include "WolfBehavior.h"
#include "WolfMovementParams.h"
#include <array>
#include <cmath>

#if FANG_ENABLE_EDITOR
#include "Editor/EditorUI.h"
#endif


FANG_DEFINE_LOG_CATEGORY(Game);


namespace fang::game
{
	namespace
	{
		/** @brief 光源が狼の周りを 1 周する秒数。 */
		constexpr float LIGHT_ORBIT_SECONDS = 10.0f;

		// 既定の光（DirectionalLight の初期値）と同じ仰角を保ち、方位だけを回す。2 つの値は既定の向き
		// (0.309, 0.722, -0.619) の水平成分の長さと高さで、正規化済みの値から取っているので回しても長さ 1 のまま。
		constexpr float LIGHT_DIRECTION_HORIZONTAL = 0.692f;
		constexpr float LIGHT_DIRECTION_HEIGHT     = 0.722f;

		/** @brief 同時に存在できるオブジェクトの数。狼 2 体 + 置き物 40 個ぶんの余白を持たせてある。 */
		constexpr uint32_t MAX_OBJECT_COUNT = 64;

		/** @brief 同時に存在できる振る舞いの数。今は狼 2 体ぶんだけ使う。 */
		constexpr uint32_t MAX_BEHAVIOR_COUNT = 8;

		/** @brief 狼の数。GameRules のとおり、動かすのは 1 匹だけで残りは置いたまま。 */
		constexpr size_t WOLF_COUNT = 2;

		/** @brief 操作する狼の席。 */
		constexpr size_t CONTROLLED_WOLF_INDEX = 0;

		// 狼 2 体のワールド XZ。Y は毎フレーム地表から決めるので持たない。1 体目はクリアリング
		// （半径 800 の平地、地表 12.0）の中心。2 体目はその外へ出して、高さの違う 2 点で正しく載ることが
		// 1 枚の画で見えるようにする（地表 78.3 ➡ 1 体目より 66.3 高い）。
		constexpr std::array<Vector3, WOLF_COUNT> WOLF_POSITIONS{
			Vector3{ 0.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1150.0f },
		};

		// カメラの追従（CameraFollowParams）が入るまでの間、移動の向きの基準は世界の +X に固定する
		// （右スティックでの旋回はカメラができてから）。
		constexpr float PLACEHOLDER_CAMERA_YAW_RADIANS = 0.0f;

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
		 *            m_lightOrbitRadians・m_scene・m_controlledWolfBehavior はそこからしか触らない。
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
				if (!m_editorUI.Initialize(context, device, window))
				{
					return false;
				}

				m_collisionWorld = context.collisionWorld;
				m_terrain        = context.terrain;

				if (!m_scene.Initialize(
						HeapAllocator::GetInstance(),
						SceneDesc{ .maxObjectCount = MAX_OBJECT_COUNT, .maxBehaviorCount = MAX_BEHAVIOR_COUNT }
					))
				{
					FANG_LOG_ERROR(Game, "Scene を初期化できなかった");
					return false;
				}

				// メッシュ側は失敗しても OnInitialize 自体は続ける。モデルが出ないだけならゲームは続けられる。
				LoadWolfModel(device, context.meshRenderer, &m_wolf);

				for (size_t index = 0; index < WOLF_COUNT; ++index)
				{
					const bool    isControlled = index == CONTROLLED_WOLF_INDEX;
					WolfBehavior* behavior     = nullptr;

					(void)CreateWolfObject(
						m_scene,
						m_wolf,
						m_wolfMovementParams,
						m_collisionWorld,
						m_terrain,
						isControlled,
						WOLF_POSITIONS[index],
						0.0f,
						&behavior
					);

					if (isControlled)
					{
						m_controlledWolfBehavior = behavior;
					}
				}

				return true;
			}

			[[nodiscard]] FrameData* OnUpdate(const FrameUpdateContext& context) override
			{
				m_editorUI.RunRequestedTestLoad(context.frameIndex);

				// 昼夜サイクルはまだ無いので、光の向きを時間で回して「毎フレーム渡せる」ことを目で確かめる。
				m_lightOrbitRadians += context.deltaTimeSeconds * (2.0f * PI / LIGHT_ORBIT_SECONDS);
				if (m_lightOrbitRadians >= 2.0f * PI)
				{
					// 積みっぱなしにすると値が大きくなるほど角度の刻みが粗くなるので、1 周ごとに戻す。
					m_lightOrbitRadians -= 2.0f * PI;
				}

				// ReadGamepadState はメインスレッドのみなので、周の頭でメインが読んだものを Runtime から受け取る。
				if (m_controlledWolfBehavior != nullptr)
				{
					m_controlledWolfBehavior->SetFrameInput(context.gamepad, PLACEHOLDER_CAMERA_YAW_RADIANS);
				}

				m_scene.Update(context.deltaTimeSeconds);

				// 当たり判定の登録と更新。Update / GetContacts は更新ジョブだけの持ち物なので、ここで完結させる。
				std::span<const ColliderProxy> colliderProxies;
				if (m_collisionWorld != nullptr)
				{
					colliderProxies = m_scene.BuildColliderProxies(context.frameAllocator);
					m_collisionWorld->Update(colliderProxies);
				}

				FrameData* frameData = NewFrame<FrameData>(context.frameAllocator);
				if (frameData == nullptr)
				{
					// フレームメモリが足りないだけなら落とさない。このフレームは既定の光・カメラで描かれる。
					return nullptr;
				}

				frameData->light.directionToLight = {
					std::sinf(m_lightOrbitRadians) * LIGHT_DIRECTION_HORIZONTAL,
					LIGHT_DIRECTION_HEIGHT,
					std::cosf(m_lightOrbitRadians) * LIGHT_DIRECTION_HORIZONTAL,
				};

				frameData->renderItems     = m_scene.BuildRenderItems(context.frameAllocator);
				frameData->colliderProxies = colliderProxies;

				return frameData;
			}

			void OnRender(const FrameRenderContext& context) override
			{
				// ImGui は NewFrame と Render を同じフレームで対にしないといけないので、組み立てもここで行う。
				m_editorUI.BuildFrame(context.window, context.deltaTimeSeconds, context.renderStatistics);
				m_editorUI.Render(context.device, context.commandList);
			}

			void OnShutdown(rhi::GraphicsDevice& device) override
			{
				m_editorUI.Shutdown(device);

				m_scene.Shutdown();

				// 狼のテクスチャは MeshRenderer::Shutdown の対象外（メッシュではないため）。持ち主のここが返す。
				device.DestroyTexture(m_wolf.normalMap);
				device.DestroyTexture(m_wolf.baseColor);
			}


		private:
			EditorUI m_editorUI; /**< エディタ UI。Release 構成では空の代役に差し替わる。 */

			float m_lightOrbitRadians = 0.0f; /**< 光源の方位角。OnUpdate（ワーカースレッド）だけが触る。 */

			Scene              m_scene;
			WolfModel          m_wolf;
			WolfMovementParams m_wolfMovementParams;

			/** @brief 借用。EngineContext から受け取ったもので、Game は寿命を持たない。 */
			CollisionWorld*         m_collisionWorld = nullptr;
			const HeightmapTerrain* m_terrain        = nullptr;

			/** @brief 操作する狼の振る舞い。寿命は m_scene が持つ。パッドの橋渡しに使う。 */
			WolfBehavior* m_controlledWolfBehavior = nullptr;
		};
	} // namespace


	int Run()
	{
		FangsOfRagnarok application;
		return RunApplication(application);
	}
} // namespace fang::game
