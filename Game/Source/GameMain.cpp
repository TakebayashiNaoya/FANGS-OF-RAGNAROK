/**
 * @file GameMain.cpp
 * @brief ゲームの起動処理。エディタを差し込む唯一の場所。
 */
#include "GameMain.h"
#include "Collision/CollisionWorld.h"
#include "Core/Log/Assert.h"
#include "Core/Math/MathConstants.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Reflection/TuningRegistry.h"
#include "Core/Reflection/TuningRow.h"
#include "Input/Gamepad.h"
#include "RHI/GraphicsDevice.h"
#include "Runtime/Application.h"
#include "Scene/CameraOcclusion.h"
#include "Scene/CharacterController.h"
#include "Scene/ItemDrop.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include "CameraFollowParameter.h"
#include "EnemyManager.h"
#include "GameLog.h"
#include "MeatManager.h"
#include "Stage.h"
#include "Wolf.h"
#include "WolfController.h"
#include "WolfManager.h"
#include "WolfMovementParameter.h"
#include "WolfTeamGrowth.h"
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

		/** @brief 同時に存在できるオブジェクトの数。狼 2 体 + 置き物 40 個 + 雑魚 32 体 + 肉 8 個で 82。余白を足してある。 */
		constexpr uint32_t MAX_OBJECT_COUNT = 128;

		/** @brief 同時に存在できる振る舞いの数。狼 2 体 + 雑魚 32 体で 34。 */
		constexpr uint32_t MAX_BEHAVIOR_COUNT = 64;

		/** @brief 狼の数。GameRules のとおり、動かすのは 1 匹だけで残りは置いたまま。 */
		constexpr size_t WOLF_COUNT = 2;

		/** @brief 湧いたときの無敵時間。最大 HP は WolfTeamGrowth::baseMaximumHitPoints から読む（つまみ）。 */
		constexpr float WOLF_INVINCIBLE_SECONDS = 0.5f;

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
		 *            m_scene・m_wolfManager はそこからしか触らない。
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

				WolfTeamGrowth*       teamGrowth = m_wolfManager.GetTeamGrowth();
				const HealthComponent wolfHealth{
					.maximumHitPoints  = teamGrowth->baseMaximumHitPoints,
					.currentHitPoints  = teamGrowth->baseMaximumHitPoints,
					.invincibleSeconds = WOLF_INVINCIBLE_SECONDS,
				};

				for (size_t index = 0; index < WOLF_COUNT; ++index)
				{
					const CharacterCreateResult<WolfController> result = CreateWolfObject(
						m_scene,
						m_wolf,
						m_wolfMovementParameter,
						m_wolfSwingParameter,
						wolfHealth,
						teamGrowth,
						m_itemDropParameter,
						m_wolfManager.GetTeamItems(),
						m_collisionWorld,
						m_terrain,
						WOLF_POSITIONS[index],
						0.0f
					);

					if (result.actor.IsValid())
					{
						(void)m_wolfManager.Add(result.actor, result.behavior);
					}
				}

				// 置き物も同じく失敗しても続ける。読めなければ置き物なしで動く。
				LoadAndCreateStageObjects(device, context.meshRenderer, m_scene, m_terrain, &m_stage);

#if FANG_ENABLE_EDITOR
				RegisterTuningValues();
#endif

				return true;
			}

			[[nodiscard]] FrameData* OnUpdate(const FrameUpdateContext& context) override
			{
				m_editorUI.RunRequestedTestLoad(context.frameIndex);

				// 生死を数え直し、操作対象を選び直す。振る舞いのポインタを誰かが触るより前に呼ぶ
				// （撃破された狼のポインタが 1 フレームも残らないようにするため）。
				const WolfManagerUpdateResult wolfManagerResult = m_wolfManager.Update();
				if (wolfManagerResult.didWipeOut)
				{
					FANG_LOG_INFO(Game, "狼が全滅した");
				}
				if (wolfManagerResult.gainedLevelCount > 0)
				{
					FANG_LOG_INFO(
						Game,
						"チームのレベルが上がった: Lv{}",
						m_wolfManager.GetTeamGrowth()->levelProgress.level
					);
				}

				// 昼夜サイクルはまだ無いので、光の向きを時間で回して「毎フレーム渡せる」ことを目で確かめる。
				// 積むのをやめて絶対の時刻から出す ➡ 上限で切られた周があっても、光の位置が実時間からずれない
				// （ADR-043）。
				const float lightOrbitRadians =
					static_cast<float>(std::fmod(context.elapsedSeconds, LIGHT_ORBIT_SECONDS)) *
					(2.0f * PI / LIGHT_ORBIT_SECONDS);

				// カメラの方位。パッドがあれば右スティック、無ければ時間で回す
				// ➡ 起動して放置しスクリーンショットを撮る確認手順がそのまま使える。
				if (context.gamepad.isConnected)
				{
					m_cameraOrbitRadians += GetRightStick(context.gamepad).x *
											m_cameraFollowParameter.yawSpeedRadiansPerSecond * context.deltaTimeSeconds;
				}
				else
				{
					m_cameraOrbitRadians +=
						context.deltaTimeSeconds * (2.0f * PI / m_cameraFollowParameter.orbitSecondsWhenDisconnected);
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
				WolfController* controlledWolf = m_wolfManager.GetControlledWolf();
				if (controlledWolf != nullptr)
				{
					controlledWolf->SetFrameInput(context.gamepad, cameraYawRadians);

					// 湧きは前フレームのワールド位置を見る（当たり判定と同じ 1 フレーム遅れ、ADR-034）。
					const Vector3 controlledWolfPosition = m_wolfManager.GetControlledActor()->GetWorldPosition();

					// 全滅中は呼ばない ➡ 狼が居なければ湧かない。標的はポインタ渡しなので、既に湧いている
					// 雑魚も次に湧く雑魚も WolfManager が選び直した操作対象へ同じフレームで移る。
					m_enemyManager.Update(
						context.deltaTimeSeconds,
						controlledWolfPosition,
						EnemyManager::Dependencies{
							.scene          = &m_scene,
							.sharedModel    = &m_wolf,
							.collisionWorld = m_collisionWorld,
							.terrain        = m_terrain,
							.target         = m_wolfManager.GetControlledActor(),
						}
					);
				}

				// 全滅中も呼ぶ（肉は歳を取り続ける。拾う側だけ無効な Actor になり、回収は起きない）。
				m_meatManager.Update(
					context.deltaTimeSeconds,
					m_itemDropParameter,
					m_wolfManager.GetTeamItems(),
					MeatManager::Dependencies{
						.scene       = &m_scene,
						.sharedModel = &m_wolf,
						.collector   = m_wolfManager.GetControlledActor(),
					}
				);

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
					std::sinf(lightOrbitRadians) * LIGHT_DIRECTION_HORIZONTAL,
					LIGHT_DIRECTION_HEIGHT,
					std::cosf(lightOrbitRadians) * LIGHT_DIRECTION_HORIZONTAL,
				};

				// 注視点は操作している狼のワールド位置（接地後の高さ）。Scene::Update の後なので今フレームの
				// 移動が反映済み。カメラは俯角を付けた円錐面を周る。水平半径は距離 × cos(俯角)、高さは
				// 距離 × sin(俯角)。水平のままだと周回の途中で丘に潜るので、俯角で視点を持ち上げてある。
				// 全滅中は操作対象が居ないので、最後に居た位置に留める
				// （無効な Actor の GetWorldPosition は原点 ➡ そのまま使うと原点へ飛ぶ）。
				if (controlledWolf != nullptr)
				{
					const Vector3 wolfPosition = m_wolfManager.GetControlledActor()->GetWorldPosition();
					m_lastCameraTarget         = wolfPosition + m_cameraFollowParameter.targetOffset;
				}
				const Vector3 cameraTarget = m_lastCameraTarget;

				const float orbitRadius =
					m_cameraFollowParameter.distanceCentimeters * std::cosf(m_cameraFollowParameter.pitchRadians);
				const Vector3 orbitOffset{
					std::sinf(m_cameraOrbitRadians) * orbitRadius,
					m_cameraFollowParameter.distanceCentimeters * std::sinf(m_cameraFollowParameter.pitchRadians),
					std::cosf(m_cameraOrbitRadians) * orbitRadius,
				};

				// 遮蔽物(置き物)が視点と注視点の間に入っていれば、方位・俯角・注視点は変えずに距離だけ寄せる。
				const CameraOcclusionResult occlusion = SolveCameraOcclusion(
					m_collisionWorld,
					m_cameraFollowParameter.occlusion,
					CameraOcclusionInput{
						.targetPosition              = cameraTarget,
						.defaultEyePosition          = cameraTarget + orbitOffset,
						.previousDistanceCentimeters = m_cameraDistanceCentimeters,
						.deltaTimeSeconds            = context.deltaTimeSeconds,
					}
				);
				m_cameraDistanceCentimeters = occlusion.distanceCentimeters;

				frameData->camera = CameraView{
					.eyePosition         = occlusion.eyePosition,
					.targetPosition      = cameraTarget,
					.fieldOfViewYRadians = m_cameraFollowParameter.fieldOfViewYRadians,
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
#if FANG_ENABLE_EDITOR
				TuningRegistry::GetInstance().Clear();
#endif

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
#if FANG_ENABLE_EDITOR
			/**
			 * @brief 調整値の実体を登録簿へ載せ、出た行の一覧をログへ流す。
			 * @details 行の本数は EXPECTED_TUNING_ROW_COUNT で固定して確かめる ➡ 登録の漏れがここで止まる。
			 */
			void RegisterTuningValues()
			{
				constexpr uint32_t EXPECTED_TUNING_ROW_COUNT = 54;

				TuningRegistry& registry = TuningRegistry::GetInstance();
				FANG_VERIFY(registry.Register("狼の移動", &m_wolfMovementParameter));
				FANG_VERIFY(registry.Register("狼の牙", &m_wolfSwingParameter));
				FANG_VERIFY(registry.Register("カメラ", &m_cameraFollowParameter));
				FANG_VERIFY(registry.Register("肉", &m_itemDropParameter));
				m_enemyManager.RegisterTuningValues();
				m_wolfManager.RegisterTuningValues(); // 登録名は「狼の成長」「肉のバッグ」

				TuningRow                  rows[MAX_TUNING_ROW_COUNT];
				const TuningRowBuildResult result = registry.BuildRows(rows);

				FANG_LOG_INFO(
					Game,
					"調整値を登録した（{} 件 / 行 {} 本 / 段で切った {} / 置き場不足 {}）",
					registry.GetEntries().size(),
					result.rowCount,
					result.depthLimitedFieldCount,
					result.droppedRowCount
				);

				for (uint32_t index = 0; index < result.rowCount; ++index)
				{
					const TuningRow& row = rows[index];

					char path[MAX_TUNING_PATH_LENGTH];
					(void)FormatTuningRowPath(row, path);

					FANG_LOG_INFO(
						Game,
						"  {} / {} / {} 段 / {}",
						registry.GetEntries()[row.entryIndex].displayName,
						path,
						row.depth,
						row.GetDisplayName()
					);
				}

				FANG_ASSERT(result.rowCount == EXPECTED_TUNING_ROW_COUNT, "登録した調整値から出る行の本数が変わった");
			}
#endif


			EditorUI m_editorUI; /**< エディタ UI。Release 構成では空の代役に差し替わる。 */

			Scene                 m_scene;
			WolfModel             m_wolf;
			StageModel            m_stage;
			WolfMovementParameter m_wolfMovementParameter;
			MeleeSwingParameter   m_wolfSwingParameter;
			CameraFollowParameter m_cameraFollowParameter;
			EnemyManager          m_enemyManager;

			/** @brief 落ちる・拾う・使う・消えるの調整値。実体はここ。狼と MeatManager の両方が読む。 */
			ItemDropParameter m_itemDropParameter;

			/** @brief 場に出ている肉の席。 */
			MeatManager m_meatManager;

			/** @brief カメラの水平回転角。右スティックが無ければ時間で回る。OnUpdate だけが触る。 */
			float m_cameraOrbitRadians = 0.0f;

			/** @brief 借用。EngineContext から受け取ったもので、Game は寿命を持たない。 */
			CollisionWorld*         m_collisionWorld = nullptr;
			const HeightmapTerrain* m_terrain        = nullptr;

			/** @brief 狼の席と、今どれを操作しているか。操作・カメラ・湧きの基準・雑魚の標的はここから引く。 */
			WolfManager m_wolfManager;

			/** @brief カメラの最後の注視点。全滅中は操作対象が居ないので、これを使い続ける。 */
			Vector3 m_lastCameraTarget;

			/** @brief 前フレームの解いたカメラ距離。0 は「まだ 1 度も解いていない」印(戻しの速度制限を掛けない)。 */
			float m_cameraDistanceCentimeters = 0.0f;
		};
	} // namespace


	int Run()
	{
		FangsOfRagnarok application;
		return RunApplication(application);
	}
} // namespace fang::game
