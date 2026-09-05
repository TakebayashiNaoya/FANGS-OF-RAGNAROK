/**
 * @file GameMain.cpp
 * @brief ゲームの起動処理。エディタを差し込む唯一の場所。
 */
#include "GameMain.h"
#include "Collision/CollisionWorld.h"
#include "Core/Math/MathConstants.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Input/Gamepad.h"
#include "RHI/GraphicsDevice.h"
#include "Runtime/Application.h"
#include "Scene/CharacterMovement.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include "CameraFollowParams.h"
#include "GameLog.h"
#include "MinionSpawner.h"
#include "Stage.h"
#include "Wolf.h"
#include "WolfBehavior.h"
#include "WolfMovementParams.h"
#include "WolfPack.h"
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

		/** @brief 同時に存在できるオブジェクトの数。狼 2 体 + 置き物 40 個 + 雑魚 32 体で 74。余白を足してある。 */
		constexpr uint32_t MAX_OBJECT_COUNT = 128;

		/** @brief 同時に存在できる振る舞いの数。狼 2 体 + 雑魚 32 体で 34。 */
		constexpr uint32_t MAX_BEHAVIOR_COUNT = 64;

		/** @brief 狼の数。GameRules のとおり、動かすのは 1 匹だけで残りは置いたまま。 */
		constexpr size_t WOLF_COUNT = 2;

		/** @brief 狼の湧いたときの HP と無敵時間。攻撃力 50 に対して 300 ➡ 12 発で倒れる。 */
		constexpr HealthComponent WOLF_HEALTH{
			.maximumHitPoints  = 300.0f,
			.currentHitPoints  = 300.0f,
			.invincibleSeconds = 0.5f,
		};

		// 狼 2 体のワールド XZ。Y は毎フレーム地表から決めるので持たない。1 体目はクリアリング
		// （半径 800 の平地、地表 12.0）の中心。2 体目はその外へ出して、高さの違う 2 点で正しく載ることが
		// 1 枚の画で見えるようにする（地表 78.3 ➡ 1 体目より 66.3 高い）。
		constexpr std::array<Vector3, WOLF_COUNT> WOLF_POSITIONS{
			Vector3{ 0.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1150.0f },
		};

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
		 *            m_lightOrbitRadians・m_scene・m_wolfPack はそこからしか触らない。
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
					WolfBehavior* behavior = nullptr;

					const GameObjectHandle handle = CreateWolfObject(
						m_scene,
						m_wolf,
						m_wolfMovementParams,
						m_wolfSwingParams,
						WOLF_HEALTH,
						m_collisionWorld,
						m_terrain,
						WOLF_POSITIONS[index],
						0.0f,
						&behavior
					);

					if (handle.IsValid())
					{
						(void)m_wolfPack.Add(handle, behavior);
					}
				}

				// 置き物も同じく失敗しても続ける。読めなければ置き物なしで動く。
				LoadAndCreateStageObjects(device, context.meshRenderer, m_scene, m_terrain, &m_stage);

				return true;
			}

			[[nodiscard]] FrameData* OnUpdate(const FrameUpdateContext& context) override
			{
				m_editorUI.RunRequestedTestLoad(context.frameIndex);

				// 生死を数え直し、操作対象を選び直す。振る舞いのポインタを誰かが触るより前に呼ぶ
				// （撃破された狼のポインタが 1 フレームも残らないようにするため）。
				const WolfPackUpdateResult wolfPackResult = m_wolfPack.Update(m_scene);
				if (wolfPackResult.didWipeOut)
				{
					FANG_LOG_INFO(Game, "狼が全滅した");
				}

				// 昼夜サイクルはまだ無いので、光の向きを時間で回して「毎フレーム渡せる」ことを目で確かめる。
				m_lightOrbitRadians += context.deltaTimeSeconds * (2.0f * PI / LIGHT_ORBIT_SECONDS);
				if (m_lightOrbitRadians >= 2.0f * PI)
				{
					// 積みっぱなしにすると値が大きくなるほど角度の刻みが粗くなるので、1 周ごとに戻す。
					m_lightOrbitRadians -= 2.0f * PI;
				}

				// カメラの方位。パッドがあれば右スティック、無ければ時間で回す
				// ➡ 起動して放置しスクリーンショットを撮る確認手順がそのまま使える。
				if (context.gamepad.isConnected)
				{
					m_cameraOrbitRadians += GetRightStick(context.gamepad).x *
											m_cameraFollowParams.yawSpeedRadiansPerSecond * context.deltaTimeSeconds;
				}
				else
				{
					m_cameraOrbitRadians +=
						context.deltaTimeSeconds * (2.0f * PI / m_cameraFollowParams.orbitSecondsWhenDisconnected);
				}

				if (m_cameraOrbitRadians >= 2.0f * PI)
				{
					// 積みっぱなしにすると値が大きくなるほど角度の刻みが粗くなるので、1 周ごとに戻す。
					m_cameraOrbitRadians -= 2.0f * PI;
				}
				else if (m_cameraOrbitRadians < 0.0f)
				{
					m_cameraOrbitRadians += 2.0f * PI;
				}

				// カメラは注視点から見て orbitOffset の位置にいる ➡ 前を向く向きはその逆。移動の基準にする
				// （前後左右を画面に合わせるため）。
				const float cameraYawRadians = GetYawFromDirection(
					Vector3{ -std::sinf(m_cameraOrbitRadians), 0.0f, -std::cosf(m_cameraOrbitRadians) }
				);

				// ReadGamepadState はメインスレッドのみなので、周の頭でメインが読んだものを Runtime から受け取る。
				// 全滅中は呼ばない ➡ 入力の受け付けが止まる。
				WolfBehavior* controlledWolfBehavior = m_wolfPack.GetControlledBehavior();
				if (controlledWolfBehavior != nullptr)
				{
					controlledWolfBehavior->SetFrameInput(context.gamepad, cameraYawRadians);

					// 湧きは前フレームのワールド行列を見る（当たり判定と同じ 1 フレーム遅れ、ADR-034）。
					const Matrix4x4 controlledWolfWorld = m_scene.GetWorldMatrix(*m_wolfPack.GetControlledHandle());
					const Vector3   controlledWolfPosition{
						controlledWolfWorld.m[3][0],
						controlledWolfWorld.m[3][1],
						controlledWolfWorld.m[3][2],
					};

					// 全滅中は呼ばない ➡ 狼が居なければ湧かない。標的はポインタ渡しなので、既に湧いている
					// 雑魚も次に湧く雑魚も WolfPack が選び直した操作対象へ同じフレームで移る。
					m_minionSpawner.Update(
						context.deltaTimeSeconds,
						controlledWolfPosition,
						MinionSpawner::Dependencies{
							.scene          = &m_scene,
							.sharedModel    = &m_wolf,
							.collisionWorld = m_collisionWorld,
							.terrain        = m_terrain,
							.targetHandle   = m_wolfPack.GetControlledHandle(),
						}
					);
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

				// 注視点は操作している狼のワールド位置（接地後の高さ）。Scene::Update の後なので今フレームの
				// 移動が反映済み。カメラは俯角を付けた円錐面を周る。水平半径は距離 × cos(俯角)、高さは
				// 距離 × sin(俯角)。水平のままだと周回の途中で丘に潜るので、俯角で視点を持ち上げてある。
				// 全滅中は操作対象が居ないので、最後に居た位置に留める
				// （無効なハンドルの GetWorldMatrix は単位行列 ➡ そのまま使うと原点へ飛ぶ）。
				if (controlledWolfBehavior != nullptr)
				{
					const Matrix4x4 wolfWorld = m_scene.GetWorldMatrix(*m_wolfPack.GetControlledHandle());
					const Vector3   wolfPosition{ wolfWorld.m[3][0], wolfWorld.m[3][1], wolfWorld.m[3][2] };
					m_lastCameraTarget = wolfPosition + m_cameraFollowParams.targetOffset;
				}
				const Vector3 cameraTarget = m_lastCameraTarget;

				const float orbitRadius =
					m_cameraFollowParams.distanceCentimeters * std::cosf(m_cameraFollowParams.pitchRadians);
				const Vector3 orbitOffset{
					std::sinf(m_cameraOrbitRadians) * orbitRadius,
					m_cameraFollowParams.distanceCentimeters * std::sinf(m_cameraFollowParams.pitchRadians),
					std::cosf(m_cameraOrbitRadians) * orbitRadius,
				};

				frameData->camera = CameraView{
					.eyePosition         = cameraTarget + orbitOffset,
					.targetPosition      = cameraTarget,
					.fieldOfViewYRadians = m_cameraFollowParams.fieldOfViewYRadians,
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

				// テクスチャは MeshRenderer::Shutdown の対象外（メッシュではないため）。持ち主のここが返す。
				device.DestroyTexture(m_wolf.normalMap);
				device.DestroyTexture(m_wolf.baseColor);
				for (const rhi::TextureHandle& textureHandle : m_stage.textures)
				{
					device.DestroyTexture(textureHandle);
				}
			}


		private:
			EditorUI m_editorUI; /**< エディタ UI。Release 構成では空の代役に差し替わる。 */

			float m_lightOrbitRadians = 0.0f; /**< 光源の方位角。OnUpdate（ワーカースレッド）だけが触る。 */

			Scene              m_scene;
			WolfModel          m_wolf;
			StageModel         m_stage;
			WolfMovementParams m_wolfMovementParams;
			MeleeSwingParams   m_wolfSwingParams;
			CameraFollowParams m_cameraFollowParams;
			MinionSpawner      m_minionSpawner;

			/** @brief カメラの水平回転角。右スティックが無ければ時間で回る。OnUpdate だけが触る。 */
			float m_cameraOrbitRadians = 0.0f;

			/** @brief 借用。EngineContext から受け取ったもので、Game は寿命を持たない。 */
			CollisionWorld*         m_collisionWorld = nullptr;
			const HeightmapTerrain* m_terrain        = nullptr;

			/** @brief 狼の席と、今どれを操作しているか。操作・カメラ・湧きの基準・雑魚の標的はここから引く。 */
			WolfPack m_wolfPack;

			/** @brief カメラの最後の注視点。全滅中は操作対象が居ないので、これを使い続ける。 */
			Vector3 m_lastCameraTarget;
		};
	} // namespace


	int Run()
	{
		FangsOfRagnarok application;
		return RunApplication(application);
	}
} // namespace fang::game
