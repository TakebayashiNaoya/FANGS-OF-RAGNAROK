/**
 * @file Application.cpp
 * @brief フレームループと初期化順。
 */
#include "Pch.h"
#include "Runtime/Application.h"
#include "Collision/CollisionDebugLines.h"
#include "Collision/CollisionWorld.h"
#include "Core/Job/JobSystem.h"
#include "Core/Log/Assert.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Memory/Allocator.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/AssetPath.h"
#include "Core/Platform/Budget.h"
#include "Core/Platform/Window.h"
#include "Core/Reflection/TuningRegistry.h"
#include "Input/Input.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/TerrainRenderer.h"
#include "Renderer/UnlitRenderer.h"
#include "Resource/DdsImage.h"
#include "Resource/HeightmapTerrain.h"
#include "Runtime/FrameClock.h"
#include "Runtime/FramePipeline.h"
#include "Runtime/RuntimeLog.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>
#include <string>
#include <vector>


FANG_DEFINE_LOG_CATEGORY(Runtime);


namespace fang
{
#if FANG_ENABLE_PROFILER
	static_assert(
		RenderStatistics::MAX_TIMED_PASS_COUNT == RenderGraph::MAX_PASS_COUNT,
		"スナップショットに載せられるパス別 GPU 時間の数が RenderGraph のパス数と合っていない"
	);
#endif


	namespace
	{
		constexpr rhi::ClearColor BACKGROUND_COLOR{ 0.05f, 0.06f, 0.09f, 1.0f };

		constexpr Vector3 CAMERA_UP{ 0.0f, 1.0f, 0.0f };

		/** @brief 垂直画角の既定値。ラジアン。Game が FrameData::camera を書いていなければこれを使う。 */
		constexpr float CAMERA_FIELD_OF_VIEW_Y_RADIANS = 60.0f * PI / 180.0f;

		// 近平面・遠平面はゲームの尺度に依らない汎用の値。近すぎると深度の精度を捨て、遠すぎると地形の
		// 対角(8192cm 四方なら 5792cm)より手前で切れてしまう。
		constexpr float CAMERA_NEAR_Z = 10.0f;
		constexpr float CAMERA_FAR_Z  = 8000.0f;

		/**
		 * @brief Game がまだ FrameData::camera を書いていないときの既定視点。
		 * @details 起動直後の 1 フレーム目や OnUpdate が失敗したフレームでも、原点付近を見下ろす絵になる
		 *          （eye と target が重ならないようにする ➡ MakeLookAtMatrix の契約を満たす）。
		 */
		constexpr CameraView DEFAULT_CAMERA{
			.eyePosition         = { 0.0f, 800.0f, -1200.0f },
			.targetPosition      = { 0.0f, 0.0f, 0.0f },
			.fieldOfViewYRadians = CAMERA_FIELD_OF_VIEW_Y_RADIANS,
		};

		/**
		 * @brief 当たり判定に登録できる数。
		 * @details 今は狼 2 体 + 置き物 40 個だが、雑魚が湧くようになる分の余白を取ってある。
		 */
		constexpr uint32_t MAX_COLLIDER_COUNT = 256;

#if FANG_ENABLE_DEBUG_DRAW
		/** @brief RenderItem の境界ボックスを表す線の色。緑系にして他の要素と見分けやすくする。 */
		constexpr Vector3 DEBUG_DRAW_BOUNDS_COLOR{ 0.0f, 1.0f, 0.3f };

		/** @brief コライダーのワイヤーの色。青系。 */
		constexpr Vector3 DEBUG_DRAW_COLLIDER_COLOR{ 0.2f, 0.6f, 1.0f };

		/** @brief シャドウの光の視錐台を表す線の色。黄系にして境界ボックスと見分けやすくする。 */
		constexpr Vector3 DEBUG_DRAW_SHADOW_FRUSTUM_COLOR{ 1.0f, 0.85f, 0.0f };
#endif

		// 地形。検証用アセットは Tools/GenerateTerrainAssets.py が生成する。
		// 寸法の規約(全長・高さスケール・中心が原点)は生成スクリプト側の定数と対で、片方だけ変えない。
		constexpr const char* TERRAIN_HEIGHTMAP_RELATIVE_PATH = "Terrain\\Heightmap.dds";
		constexpr const char* TERRAIN_SPLATMAP_RELATIVE_PATH  = "Terrain\\Splatmap.dds";

		/** @brief レイヤのアルベド。並びはスプラットの重み(R = 草、G = 岩、B = 土)と対。 */
		constexpr const char* TERRAIN_LAYER_RELATIVE_PATHS[3] = {
			"Terrain\\LayerGrass.dds",
			"Terrain\\LayerRock.dds",
			"Terrain\\LayerDirt.dds",
		};

		/** @brief レイヤの法線マップ。並びはアルベドと対。欠けても地形は描ける(平坦なダミーが差さる)。 */
		constexpr const char* TERRAIN_LAYER_NORMAL_RELATIVE_PATHS[3] = {
			"Terrain\\LayerGrassNormal.dds",
			"Terrain\\LayerRockNormal.dds",
			"Terrain\\LayerDirtNormal.dds",
		};

		constexpr float TERRAIN_TOTAL_SIZE_CENTIMETERS   = 8192.0f;
		constexpr float TERRAIN_HEIGHT_SCALE_CENTIMETERS = 600.0f;

		/** @brief レイヤのテクスチャ 1 枚が受け持つ辺長。4m ごとの繰り返しなら近接しても粗さが目立たない。 */
		constexpr float TERRAIN_LAYER_TILE_CENTIMETERS = 400.0f;

		// 地形は静的なので設定値はここに直書きする(実行時に変える口を増やさない)。
		// パラメータの JSON 化はエディタのシーン保存と一緒に行う。
		/** @brief レイヤごとの知覚 roughness。並びは草・岩・土。岩だけ少しハイライトを残す。 */
		constexpr float TERRAIN_LAYER_ROUGHNESS[3] = { 0.9f, 0.75f, 0.95f };

		/**
		 * @brief 地形 1 式の持ち物。
		 * @details HeightmapTerrain は高さの問い合わせ(接地に使う)のため、GPU 化が済んでも持ち続ける。
		 */
		struct TerrainModel
		{
			HeightmapTerrain heightmap;

			/** @brief スプラットマップ。読めなかったら無効なままで、そのとき地形は描かれない。 */
			rhi::TextureHandle splatmap;

			/** @brief レイヤのアルベド。並びは草・岩・土。 */
			rhi::TextureHandle layerAlbedos[3];

			/** @brief レイヤの法線マップ。並びはアルベドと同じ。欠けたものは無効なままにする。 */
			rhi::TextureHandle layerNormals[3];

			/** @brief 読み込みから CreateTerrain まで通ったか。false なら地形なしで動いている。 */
			bool isLoaded = false;
		};


		/**
		 * @brief 地形用のテクスチャを 1 枚読んで GPU へ載せる。
		 * @param relativePath  アセットの根っこからの相対パス。
		 * @param outTexelWidth 読めたときだけ画像の横テクセル数を書く。要らなければ nullptr。
		 * @return 失敗したら無効なハンドル。理由は DdsImage / RHI 側がログに出す。
		 */
		[[nodiscard]] rhi::TextureHandle LoadTerrainTexture(
			rhi::GraphicsDevice& device,
			const char*          relativePath,
			uint32_t*            outTexelWidth
		)
		{
			const std::string filePath = MakeAssetPath(relativePath);

			DdsImage image;
			if (!image.Load(filePath.c_str()))
			{
				return rhi::TextureHandle{};
			}

			if (outTexelWidth != nullptr)
			{
				*outTexelWidth = image.GetMipLevels()[0].width;
			}

			const rhi::TextureSource source{
				.mipLevels = image.GetMipLevels(),
				.format    = image.GetFormat(),
			};

			return device.CreateTexture2D(source);
		}


		/**
		 * @brief 地形を読んで GPU へ載せる。
		 * @details 失敗しても落とさない。isLoaded が false のままになり、地形なしで起動が続く
		 *          (エディタは今までどおり)。理由は各段階がログに出す。
		 */
		void LoadTerrain(rhi::GraphicsDevice& device, TerrainRenderer& terrainRenderer, TerrainModel* outTerrain)
		{
			//------------------------------------------------------------------------
			// 1. ハイトマップの読み込みとチャンク生成
			// 　R16 の DDS を読み、ワールド座標の頂点・法線・インデックス・AABB を持つチャンク列を作る。
			//------------------------------------------------------------------------
			const HeightmapTerrainDesc terrainDesc{
				.totalWidth  = TERRAIN_TOTAL_SIZE_CENTIMETERS,
				.totalDepth  = TERRAIN_TOTAL_SIZE_CENTIMETERS,
				.heightScale = TERRAIN_HEIGHT_SCALE_CENTIMETERS,
			};

			const std::string heightmapPath = MakeAssetPath(TERRAIN_HEIGHTMAP_RELATIVE_PATH);
			if (!outTerrain->heightmap.Load(heightmapPath.c_str(), terrainDesc))
			{
				FANG_LOG_ERROR(Runtime, "ハイトマップを読めなかった。地形なしで続ける: {}", heightmapPath);
				return;
			}

			//------------------------------------------------------------------------
			// 2. スプラットマップとレイヤアルベドの読み込み
			// 　1 枚でも欠けたら地形なしにする(欠けたレイヤだけ単色で補うような分岐を増やさない)。
			//------------------------------------------------------------------------
			uint32_t splatTexelCount = 0;
			outTerrain->splatmap     = LoadTerrainTexture(device, TERRAIN_SPLATMAP_RELATIVE_PATH, &splatTexelCount);

			bool hasAllTextures = outTerrain->splatmap.IsValid();
			for (size_t index = 0; index < 3; ++index)
			{
				outTerrain->layerAlbedos[index] =
					LoadTerrainTexture(device, TERRAIN_LAYER_RELATIVE_PATHS[index], nullptr);
				hasAllTextures = hasAllTextures && outTerrain->layerAlbedos[index].IsValid();
			}

			if (!hasAllTextures)
			{
				FANG_LOG_ERROR(Runtime, "地形のテクスチャがそろわなかった。地形なしで続ける");
				return;
			}

			// 法線マップは欠けても地形なしにしない ➡ 凹凸が出ないだけで、絵は今までどおり出る。
			for (size_t index = 0; index < 3; ++index)
			{
				outTerrain->layerNormals[index] =
					LoadTerrainTexture(device, TERRAIN_LAYER_NORMAL_RELATIVE_PATHS[index], nullptr);
				if (!outTerrain->layerNormals[index].IsValid())
				{
					FANG_LOG_WARNING(
						Runtime,
						"地形レイヤの法線マップを読めなかった。凹凸なしで描く: {}",
						TERRAIN_LAYER_NORMAL_RELATIVE_PATHS[index]
					);
				}
			}

			//------------------------------------------------------------------------
			// 3. チャンクの詰め替えと GPU 化
			// 　Resource の生成結果(TerrainChunkSource)を Renderer の受け口(TerrainChunk)へ写し、
			// 　CreateTerrain で圧縮頂点にして載せる。読み込み時なのでここのヒープ確保は許す。
			//------------------------------------------------------------------------
			const std::span<const TerrainChunkSource> chunkSources = outTerrain->heightmap.GetChunks();

			std::vector<TerrainChunk> chunks;
			chunks.reserve(chunkSources.size());
			for (const TerrainChunkSource& source : chunkSources)
			{
				chunks.push_back(
					TerrainChunk{
						.positions = source.positions,
						.normals   = source.normals,
						.indices   = source.indices,
						.bounds    = source.bounds,
					}
				);
			}

			const TerrainSurface surface{
				.splatmap       = outTerrain->splatmap,
				.layerAlbedos   = { outTerrain->layerAlbedos[0],
									outTerrain->layerAlbedos[1],
									outTerrain->layerAlbedos[2] },
				.layerNormals   = { outTerrain->layerNormals[0],
									outTerrain->layerNormals[1],
									outTerrain->layerNormals[2] },
				.layerRoughness = { TERRAIN_LAYER_ROUGHNESS[0],
									TERRAIN_LAYER_ROUGHNESS[1],
									TERRAIN_LAYER_ROUGHNESS[2] },

				.layerTileCentimeters = TERRAIN_LAYER_TILE_CENTIMETERS,
				.halfWidth            = TERRAIN_TOTAL_SIZE_CENTIMETERS * 0.5f,
				.halfDepth            = TERRAIN_TOTAL_SIZE_CENTIMETERS * 0.5f,
				.splatTexelCount      = splatTexelCount,
			};

			if (!terrainRenderer.CreateTerrain(device, chunks, surface))
			{
				FANG_LOG_ERROR(Runtime, "地形を GPU に載せられなかった。地形なしで続ける");
				return;
			}

			outTerrain->isLoaded = true;
		}


		/** @brief FramePipeline へ渡す、フレームループの持ち物。 */
		struct FrameLoopContext
		{
			IApplication*        application   = nullptr;
			rhi::GraphicsDevice* device        = nullptr;
			Window*              window        = nullptr;
			JobSystem*           jobSystem     = nullptr;
			RenderGraph*         renderGraph   = nullptr;
			SceneRenderer*       sceneRenderer = nullptr;
			UnlitRenderer*       unlitRenderer = nullptr;
#if FANG_ENABLE_DEBUG_DRAW
			DebugDraw* debugDraw = nullptr;
#endif
			TerrainRenderer* terrainRenderer = nullptr;

			/** @brief RunApplication が持つ入れ物。graph.Execute の戻り値の後に 4 値を書く。 */
			RenderStatistics* renderStatistics = nullptr;

			/** @brief バックバッファを取れなかったフレームで立つ。ループを抜ける合図。 */
			bool hasDeviceError = false;
		};


		/** @brief 更新の本体。ワーカースレッドで走るので、渡された束の外へは手を伸ばさない。 */
		FrameData* UpdateFrame(void* userData, const FrameUpdateContext& context)
		{
			auto& loopContext = *static_cast<FrameLoopContext*>(userData);
			return loopContext.application->OnUpdate(context);
		}

		/** @brief 三角形パスの記録関数に渡す入力。 */
		struct UnlitPassRecordArguments
		{
			UnlitRenderer*       unlitRenderer = nullptr;
			rhi::GraphicsDevice* device        = nullptr;
		};

		/**
		 * @brief 三角形を積む。
		 * @details 頂点をクリップ空間で持っているので、恒等行列（Matrix4x4 の既定値）を渡してそのまま通す。
		 */
		void RecordUnlitPass(void* userData, rhi::CommandList& commandList)
		{
			const auto& arguments = *static_cast<const UnlitPassRecordArguments*>(userData);
			arguments.unlitRenderer->DrawTriangle(*arguments.device, commandList, Matrix4x4{});
		}

#if FANG_ENABLE_EDITOR
		/** @brief エディタパスの記録関数に渡す入力。従来 FrameRenderContext に積んでいたものと同じ中身。 */
		struct EditorPassRecordArguments
		{
			IApplication*           application      = nullptr;
			rhi::GraphicsDevice*    device           = nullptr;
			const Window*           window           = nullptr;
			const FrameData*        frameData        = nullptr;
			const RenderStatistics* renderStatistics = nullptr;
			uint64_t                frameIndex       = 0;
			float                   deltaTimeSeconds = 0.0f;
		};

		/**
		 * @brief 上の層に描画コマンドを積ませる。
		 * @details ImGui はメインスレッドでしか触れないので、このパスは recordThread = Main で宣言する
		 *          （01 §9.2）。IApplication の契約は変えず、従来どおり FrameRenderContext を組んで渡す。
		 */
		void RecordEditorPass(void* userData, rhi::CommandList& commandList)
		{
			const auto& arguments = *static_cast<const EditorPassRecordArguments*>(userData);

			const FrameRenderContext context{
				*arguments.device,           commandList,          *arguments.window,          arguments.frameData,
				*arguments.renderStatistics, arguments.frameIndex, arguments.deltaTimeSeconds,
			};

			arguments.application->OnRender(context);
		}
#endif

		/** @brief 描画の本体。RHI を触るのはここだけなので、メインスレッドの持ち物が全部そろっている。 */
		void RenderFrame(void* userData, const FrameData* frameData, uint64_t frameIndex, float deltaTimeSeconds)
		{
			// 全構成で使われるとは限らない（ホットリロード・プロファイラ・エディタが軒並み無効な Release では
			// 未使用になる）。#if で分岐して片方でだけ使わなくなる引数なので FANG_UNUSED で明示する。
			FANG_UNUSED(deltaTimeSeconds);

			auto& loopContext = *static_cast<FrameLoopContext*>(userData);

			rhi::GraphicsDevice& device        = *loopContext.device;
			Window&              window        = *loopContext.window;
			JobSystem&           jobSystem     = *loopContext.jobSystem;
			RenderGraph&         graph         = *loopContext.renderGraph;
			SceneRenderer&       sceneRenderer = *loopContext.sceneRenderer;

#if FANG_ENABLE_PROFILER
			// 描画（メイン）の内訳の 1 つ目。ここから EndFrame を呼ぶ手前までが「記録」。
			const auto recordStartTime = std::chrono::steady_clock::now();
#endif

			//------------------------------------------------------------------------
			// 1. リサイズ処理
			// 　ウィンドウの大きさが変わっていたら、スワップチェーンと深度バッファをその大きさで作り直す。
			// 　変わっていなければ何もしない。
			//------------------------------------------------------------------------
			// リサイズは更新と関わらないので、ジョブを投げた後のここで済ませる。BeginFrame の中では作り直せない。
			if (window.ConsumeSizeChange())
			{
				device.Resize(window.GetWidth(), window.GetHeight());
			}

#if FANG_ENABLE_HOT_RELOAD
			// .hlsl の保存を見て PSO を作り直す。リサイズと同じ理由でここに置く
			// （記録が始まった後ではパイプラインを差し替えられない）。保存が無ければ何も起きない。
			device.UpdateShaderHotReload(deltaTimeSeconds);
#endif

			//------------------------------------------------------------------------
			// 2. BeginFrame とグラフの Reset・バックバッファと深度の Import
			// 　device.BeginFrame() でこのフレームの記録メモリを巻き戻す。
			// 　RenderGraph 側も Reset で前フレームの宣言を捨て、ImportBackBuffer / ImportDepthBuffer /
			// 　ImportDepthTexture でバックバッファ・深度バッファ・シャドウマップをこのフレームのリソースとして
			// 　登録し直す(パスの宣言はこの後)。
			//------------------------------------------------------------------------
			// このフレームの記録メモリを巻き戻すだけ。バリアもクリアも描画先の設定もしない
			// （どのパスが何に書くかはこの後の宣言で決まる。積むのはグラフの仕事）。
			device.BeginFrame();

			graph.Reset();
			const RenderGraphResourceId backBufferResource  = graph.ImportBackBuffer();
			const RenderGraphResourceId depthBufferResource = graph.ImportDepthBuffer();
			const RenderGraphResourceId shadowMapResource   = graph.ImportDepthTexture(
				sceneRenderer.GetShadowMapTexture(),
				SceneRenderer::SHADOW_MAP_SIZE,
				SceneRenderer::SHADOW_MAP_SIZE
			);

			//------------------------------------------------------------------------
			// 3. View の組み立て
			// 　光とカメラは 1 つ前のフレームの更新が FrameData へ書いたもの。まだ何も書かれていなければ
			// 　既定値で描く（Game が 1 フレーム目にまだ書けていなくても真っ黒・原点直視にはならない）。
			//------------------------------------------------------------------------
			sceneRenderer.Reset();

			const DirectionalLight  defaultLight{};
			const DirectionalLight& light = frameData != nullptr ? frameData->light : defaultLight;

			// fieldOfViewYRadians が 0 のままなら Game がまだ camera を書いていない印。その場合は eye /
			// target も含めて丸ごと既定値に差し替える（eye と target だけ既定に戻すと、書きかけの片方だけ
			// 汎用の値になって組み合わせが化ける）。
			const bool        hasCameraFromGame = frameData != nullptr && frameData->camera.fieldOfViewYRadians > 0.0f;
			const CameraView& camera            = hasCameraFromGame ? frameData->camera : DEFAULT_CAMERA;

			// 最小化すると幅も高さも 0 で来る。ゼロ除算と MakePerspectiveMatrix のアサートを避けて 1 で止める。
			const float viewportWidth  = static_cast<float>(window.GetWidth() > 0 ? window.GetWidth() : 1);
			const float viewportHeight = static_cast<float>(window.GetHeight() > 0 ? window.GetHeight() : 1);

			const View view{
				.viewProjection = Multiply(
					MakeLookAtMatrix(camera.eyePosition, camera.targetPosition, CAMERA_UP),
					MakePerspectiveMatrix(
						camera.fieldOfViewYRadians,
						viewportWidth / viewportHeight,
						CAMERA_NEAR_Z,
						CAMERA_FAR_Z
					)
				),
				.cameraPosition   = camera.eyePosition,
				.directionToLight = light.directionToLight,
				.lightColor       = light.color,
				.lightIntensity   = light.intensity,
				.ambientColor     = light.ambientColor,
			};

			//------------------------------------------------------------------------
			// 4. シャドウ View とシーン View の登録・Submit
			// 　castsShadow かつ bounds が有効な RenderItem を union してキャスタの箱を作り、AddShadowView を
			// 　AddView より先に呼ぶ(シーン View の b1 に光の行列を焼き込む契約のため)。RenderItem 列は
			// 　Game の更新ジョブが Scene::BuildRenderItems でフレームメモリへ組み立てたもの。
			//------------------------------------------------------------------------
			const std::span<const RenderItem> submittedItems =
				frameData != nullptr ? frameData->renderItems : std::span<const RenderItem>{};

			// ヒープ確保をしない Aabb::Expand の積み重ねで箱を作る。キャスタが 1 つも無ければ無効な箱の
			// ままで、AddShadowView が無効な ViewId を返す。
			Aabb castersBounds;
			for (const RenderItem& item : submittedItems)
			{
				if (item.castsShadow && item.bounds.IsValid())
				{
					castersBounds.Expand(item.bounds.min);
					castersBounds.Expand(item.bounds.max);
				}
			}

			const ViewId shadowViewId = sceneRenderer.AddShadowView(device, light.directionToLight, castersBounds);
			const ViewId sceneViewId  = sceneRenderer.AddView(device, view);

			sceneRenderer.Submit(sceneViewId, submittedItems);

			// キャスタが 1 つも無いフレームは無効な ViewId が返る。Submit は登録済みの番号しか受け付けない
			// ので、ここで弾いて何もしない（そのフレームは影なしで描かれる）。
			if (shadowViewId.IsValid())
			{
				sceneRenderer.Submit(shadowViewId, submittedItems);
			}

			//------------------------------------------------------------------------
			// 5. Unlit パスの宣言(クリアを持つ・三角形が先の理由は既存コメントにある)
			// 　三角形を積む UnlitPass を宣言する。バックバッファと深度、両方のクリアをこのパスが引き受ける。
			//------------------------------------------------------------------------
			// 三角形を先に描き、深度を持つものが上に乗る前後関係を保つ。三角形は深度テストを持たないので、
			// 後に描くと常に上書きしてしまう ➡ 画面のクリアもこのパスが引き受ける。
			UnlitPassRecordArguments unlitArguments{
				.unlitRenderer = loopContext.unlitRenderer,
				.device        = &device,
			};

			RenderGraphPassDesc unlitPassDesc{};
			unlitPassDesc.name               = "UnlitPass";
			unlitPassDesc.recordThread       = EnPassRecordThread::Job;
			unlitPassDesc.colorTarget        = backBufferResource;
			unlitPassDesc.colorLoadOperation = EnLoadOperation::Clear;
			unlitPassDesc.clearColor         = BACKGROUND_COLOR;
			unlitPassDesc.depthTarget        = depthBufferResource;
			unlitPassDesc.depthLoadOperation = EnLoadOperation::Clear;
			unlitPassDesc.record             = &RecordUnlitPass;
			unlitPassDesc.userData           = &unlitArguments;

			graph.AddPass(unlitPassDesc);

			//------------------------------------------------------------------------
			// 6. ScenePass の宣言(Load)
			// 　RenderItem を描く ScenePass を View ごとに宣言する。
			//------------------------------------------------------------------------
			// 三角形パスが画面をクリア済みなので、最初の View も Load（前のパスが描いた画の上に重ねる）。
			sceneRenderer.AddPasses(
				graph,
				backBufferResource,
				depthBufferResource,
				shadowMapResource,
				BACKGROUND_COLOR,
				EnLoadOperation::Load
			);

			//------------------------------------------------------------------------
			// 7. TerrainPass の宣言(Load・シャドウマップ読み)
			// 　地形を描く TerrainPass を ScenePass の直後に宣言する。前後関係は深度テストが解決するので
			// 　順序に意味は無いが、シャドウマップを読むリソースとして宣言することで ShadowPass との
			// 　バリアを Compile に導かせる。b1 はシーン View の実体を借りる ➡ 光と影が建物と一致する。
			// 　地形を読めていないときは HasTerrain が false で、パスごと宣言しない。
			//------------------------------------------------------------------------
			TerrainRenderer& terrainRenderer = *loopContext.terrainRenderer;
			if (terrainRenderer.HasTerrain())
			{
				terrainRenderer.AddPass(
					graph,
					backBufferResource,
					depthBufferResource,
					shadowMapResource,
					sceneRenderer.GetFrameConstantBuffer(sceneViewId),
					sceneRenderer.GetShadowMapTexture(),
					view.viewProjection
				);
			}

#if FANG_ENABLE_DEBUG_DRAW
			//------------------------------------------------------------------------
			// 8. デバッグ描画の積み込みと DebugLinePass の宣言(FANG_ENABLE_DEBUG_DRAW 内)
			// 　コライダーの形は Game の更新ジョブが Scene::BuildColliderProxies で組み立てたもの。
			// 　CollisionWorld::Update / Query は更新ジョブだけの持ち物になったので、メインスレッドの
			// 　ここから触れない ➡ 接触の有無での色分けは行わず、コライダーは 1 色で出す。
			// 　Release では FANG_ENABLE_DEBUG_DRAW が 0 になり、この区画ごとビルドから外れる。
			//------------------------------------------------------------------------
			DebugDraw& debugDraw = *loopContext.debugDraw;

			debugDraw.Reset();

			const std::span<const ColliderProxy> colliderProxies =
				frameData != nullptr ? frameData->colliderProxies : std::span<const ColliderProxy>{};

			if (!colliderProxies.empty())
			{
				DebugLineSegment segments[MAX_SHAPE_LINE_COUNT];
				for (const ColliderProxy& proxy : colliderProxies)
				{
					const uint32_t lineCount = BuildShapeLines(proxy.shape, segments);
					for (uint32_t lineIndex = 0; lineIndex < lineCount; ++lineIndex)
					{
						debugDraw.AddLine(segments[lineIndex].from, segments[lineIndex].to, DEBUG_DRAW_COLLIDER_COLOR);
					}
				}
			}
			else
			{
				// 当たり判定が無い（Game がまだ登録していない）ときは、境界ボックスを出す。
				for (const RenderItem& item : submittedItems)
				{
					if (item.bounds.IsValid())
					{
						debugDraw.AddWireBox(item.bounds, DEBUG_DRAW_BOUNDS_COLOR);
					}
				}
			}

			// キャスタが 1 つも無いフレームは視錐台も無い（GetShadowFrustumCorners が空の span を返す）ので、
			// shadowViewId で先に弾く。
			if (shadowViewId.IsValid())
			{
				debugDraw.AddWireBoxCorners(sceneRenderer.GetShadowFrustumCorners(), DEBUG_DRAW_SHADOW_FRUSTUM_COLOR);
			}

			debugDraw.AddPass(graph, backBufferResource, depthBufferResource, view.viewProjection);
#endif

#if FANG_ENABLE_EDITOR
			//------------------------------------------------------------------------
			// 9. エディタパスの宣言(FANG_ENABLE_EDITOR 内・Main 記録)
			// 　エディタパスを宣言する。Release ビルドでは FANG_ENABLE_EDITOR が 0 になり、この区画ごと
			// 　ビルドから外れる。
			//------------------------------------------------------------------------
			// 上の層に描画コマンドを積ませる。読ませるのは 1 つ前のフレームの更新が作ったもの。
			// ImGui のメインスレッド制約に合わせて Main パスで宣言する。Release では宣言自体をしない
			// （最終 PRESENT への遷移は Compile が最後に使ったパスの末尾に出すので、これで正しく動く）。
			EditorPassRecordArguments editorArguments{
				.application      = loopContext.application,
				.device           = &device,
				.window           = &window,
				.frameData        = frameData,
				.renderStatistics = loopContext.renderStatistics,
				.frameIndex       = frameIndex,
				.deltaTimeSeconds = deltaTimeSeconds,
			};

			RenderGraphPassDesc editorPassDesc{};
			editorPassDesc.name               = "EditorPass";
			editorPassDesc.recordThread       = EnPassRecordThread::Main;
			editorPassDesc.colorTarget        = backBufferResource;
			editorPassDesc.colorLoadOperation = EnLoadOperation::Load;
			editorPassDesc.depthTarget        = depthBufferResource;
			editorPassDesc.depthLoadOperation = EnLoadOperation::Load;
			editorPassDesc.record             = &RecordEditorPass;
			editorPassDesc.userData           = &editorArguments;

			graph.AddPass(editorPassDesc);
#endif

			//------------------------------------------------------------------------
			// 10. Compile と Execute
			// 　宣言したパスから Compile でバリアとクリアの手順を導き、Execute でコマンドリストへ記録する。
			//------------------------------------------------------------------------
			graph.Compile();
			graph.Execute(device, jobSystem);

			//------------------------------------------------------------------------
			// 11. レンダリング統計のスナップショット更新
			// 　Execute の Wait が済んだこの地点でだけ、Submit 数・描いた数・パス数・コマンドリスト本数を
			// 　安全に読める（ScenePass の記録がまだ書き込み中の可能性がある間は読めない）。ここで書いた値は
			// 　次のフレームの EditorPass が読む、1 フレーム遅れのスナップショットになる。
			//------------------------------------------------------------------------
			loopContext.renderStatistics->submittedItemCount     = sceneRenderer.GetSubmittedItemCount();
			loopContext.renderStatistics->drawnItemCount         = sceneRenderer.GetLastDrawnItemCount();
			loopContext.renderStatistics->drawnTerrainChunkCount = terrainRenderer.GetLastDrawnChunkCount();
			loopContext.renderStatistics->passCount              = graph.GetPassCount();
			loopContext.renderStatistics->commandListCount = static_cast<uint32_t>(graph.GetCommandLists().size());

			//------------------------------------------------------------------------
			// 12. コマンドリストを借りられなかったときの畳み
			// 　パスを宣言したのにコマンドリストが 1 本も返らなかった(主にデバイスロスト)ら、このフレームは
			// 　EndFrame を呼ばずに畳む。
			//------------------------------------------------------------------------
			if (graph.GetPassCount() > 0 && graph.GetCommandLists().empty())
			{
				// ここに来るのは主にデバイスロスト。黙って畳むと「起動してすぐ閉じた」ようにしか見えないので残す。
				FANG_LOG_ERROR(
					Runtime,
					"フレーム {} でコマンドリストを借りられなかった。フレームループを畳む",
					frameIndex
				);
				loopContext.hasDeviceError = true;
				return;
			}

			//------------------------------------------------------------------------
			// 13. EndFrame
			// 　積んだコマンドリストを渡して実行・Present・GPU の完了待ちをまとめて行う。
			//------------------------------------------------------------------------
#if FANG_ENABLE_PROFILER
			const float recordMilliseconds =
				std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recordStartTime).count();
#endif

			device.EndFrame(graph.GetCommandLists());

#if FANG_ENABLE_PROFILER
			//------------------------------------------------------------------------
			// 14. GPU 時間と EndFrame の内訳のスナップショット更新
			// 　どちらも EndFrame が GPU の完了を待った後でしか確定しないので、区画 11 とは別にここで書く。
			// 　区画 12 で畳んだフレームは前の値が残るだけで、読み手に特別扱いは要らない。
			//------------------------------------------------------------------------
			RenderStatistics& statistics = *loopContext.renderStatistics;

			RenderGraphPassGpuTime passGpuTimes[RenderStatistics::MAX_TIMED_PASS_COUNT];
			float                  gpuFrameMilliseconds = 0.0f;

			const uint32_t timedPassCount = graph.ReadPassGpuTimes(device, passGpuTimes, &gpuFrameMilliseconds);
			for (uint32_t passIndex = 0; passIndex < timedPassCount; ++passIndex)
			{
				RenderPassGpuTime& destination = statistics.passGpuTimes[passIndex];

				// 名前の指す先は Execute までしか生きていないので、固定長の配列へ写す。余りは 0 で埋めておく。
				const std::string_view name       = passGpuTimes[passIndex].name;
				const size_t           copyLength = std::min(name.size(), RenderPassGpuTime::MAX_NAME_LENGTH - 1);
				std::memset(destination.name, 0, RenderPassGpuTime::MAX_NAME_LENGTH);
				std::memcpy(destination.name, name.data(), copyLength);

				destination.milliseconds = passGpuTimes[passIndex].milliseconds;
			}

			statistics.timedPassCount       = timedPassCount;
			statistics.gpuFrameMilliseconds = gpuFrameMilliseconds;
			statistics.hasGpuTimestamps     = device.HasGpuTimestamps();

			const rhi::EndFrameTiming& endFrameTiming = device.GetLastEndFrameTiming();

			statistics.recordMilliseconds  = recordMilliseconds;
			statistics.presentMilliseconds = endFrameTiming.presentMilliseconds;
			statistics.gpuWaitMilliseconds = endFrameTiming.gpuWaitMilliseconds;
#endif
		}
	} // namespace


	int RunApplication(IApplication& application)
	{
		//------------------------------------------------------------------------
		// 1. ジョブシステム
		// 　ワーカースレッドを起動する。並列処理すべての土台なので最初に作る。
		//------------------------------------------------------------------------
		// ジョブシステムはここが持ち、使う側へ参照で渡す。Engine ができたらそこへぶら下げ直す。
		JobSystem jobSystem;
		if (!jobSystem.Initialize(JobSystemDesc{}))
		{
			FANG_FATAL("ジョブシステムを開始できなかった");
		}

		//------------------------------------------------------------------------
		// 2. フレームメモリ
		// 　1 フレームで使い捨てる割り当ての置き場(リニアアロケータ)。
		// 　更新と描画が並走するので、フレームごとに面を分けて持つ。
		//------------------------------------------------------------------------
		// フレームメモリもここが持つ。JobSystem と同じく、使う側へは参照で渡す。
		FrameMemory frameMemory;
		if (!frameMemory.Initialize(FrameMemoryDesc{}))
		{
			FANG_FATAL("フレームメモリを確保できなかった");
		}

		FANG_LOG_INFO(
			Runtime,
			"フレームメモリを確保した (1 枚 {} KiB × {} 枚)",
			frameMemory.GetCapacityPerBuffer() / 1024,
			FrameMemory::BUFFER_COUNT
		);

		//------------------------------------------------------------------------
		// 3. ウィンドウ
		// 　OS のウィンドウを作る。次のデバイス生成がウィンドウハンドルを要求する。
		//------------------------------------------------------------------------
		Window window;
		if (!window.Initialize(WindowDesc{}))
		{
			FANG_FATAL("ウィンドウを作れなかった");
		}

		//------------------------------------------------------------------------
		// 4. D3D12 デバイス
		// 　GPU との接続と、描画に要る道具一式(スワップチェーン・深度・コマンド記録)を作る。
		// 　中の手順は GraphicsDevice::Initialize の 1〜10 を参照。
		//------------------------------------------------------------------------
		rhi::GraphicsDeviceDesc deviceDesc{};
		deviceDesc.windowHandle        = window.GetNativeHandle();
		deviceDesc.width               = window.GetWidth();
		deviceDesc.height              = window.GetHeight();
		deviceDesc.isDebugLayerEnabled = FANG_ENABLE_GPU_VALIDATION != 0;

		rhi::GraphicsDevice device;
		if (!device.Initialize(deviceDesc))
		{
			FANG_FATAL("D3D12 デバイスを作れなかった");
		}

		//------------------------------------------------------------------------
		// 5. レンダラ一式
		// 　Unlit(頂点色の三角形)、地形、MeshRenderer(Game が読み込む狼・置き物の器)、当たり判定の器、
		// 　RenderGraph(パスとバリアの管理)、SceneRenderer(View とカリング)を順に用意する。
		//------------------------------------------------------------------------
		UnlitRenderer unlitRenderer;
		if (!unlitRenderer.Initialize(device))
		{
			FANG_FATAL("頂点色描画の準備に失敗した");
		}

#if FANG_ENABLE_DEBUG_DRAW
		// デバッグ用の可視化なので、失敗しても FANG_FATAL にせず可視化だけを飛ばして続ける。
		DebugDraw debugDraw;
		if (!debugDraw.Initialize(device))
		{
			FANG_LOG_ERROR(Runtime, "デバッグ描画の準備に失敗した。可視化だけを飛ばす");
		}
#endif

		// 地形は失敗を FANG_FATAL にしない。読めなければ地形なしで起動が続く。
		TerrainRenderer terrainRenderer;
		TerrainModel    terrain;
		if (terrainRenderer.Initialize(device))
		{
			LoadTerrain(device, terrainRenderer, &terrain);
		}
		else
		{
			FANG_LOG_ERROR(Runtime, "地形描画の準備に失敗した。地形の表示だけを飛ばす");
		}

		// メッシュの読み込みは Game の仕事。ここでは器を用意するだけ。失敗しても FANG_FATAL にしない
		// （モデルが出ないだけならゲームは続けられるし、起動できないほうが困るため。三角形とエディタは動く）。
		MeshRenderer meshRenderer;
		if (!meshRenderer.Initialize(device))
		{
			FANG_LOG_ERROR(Runtime, "メッシュ描画の準備に失敗した。モデルの表示だけを飛ばす");
		}

		// 当たり判定も失敗を FANG_FATAL にしない。作れなければ判定と可視化だけを飛ばし、絵は今までどおり出る。
		CollisionWorld collisionWorld;
		const bool     hasCollisionWorld = collisionWorld.Initialize(
			HeapAllocator::GetInstance(),
			CollisionWorldDesc{ .maxColliderCount = MAX_COLLIDER_COUNT }
		);
		if (!hasCollisionWorld)
		{
			FANG_LOG_ERROR(Runtime, "当たり判定の準備に失敗した。判定と可視化だけを飛ばす");
		}

		// RenderGraph はフレームごとに Reset して組み直す入れ物なので、器そのものはここで 1 つだけ作る。
		RenderGraph renderGraph;

		// SceneRenderer も同じく器を使い回す。meshRenderer の Initialize の成否によらず作る
		// （失敗していても MeshRenderer::Draw 側が無効なハンドルを見て安全に何もしない）。
		SceneRenderer sceneRenderer;
		if (!sceneRenderer.Initialize(device, meshRenderer))
		{
			FANG_LOG_ERROR(Runtime, "シーン描画の準備に失敗した。モデルの表示だけを飛ばす");
		}

		// 起動 1 フレーム目は既定値（全部 0）のまま EditorPass へ渡る。1 フレーム遅れのスナップショットなので
		// 特別扱いは要らない。
		RenderStatistics renderStatistics;

		//------------------------------------------------------------------------
		// 6. フレームパイプライン
		// 　更新(ジョブ)と描画(メインスレッド)を並走させる仕組み。
		// 　描画が読むのは 1 つ前のフレームの更新が作った FrameData。
		// 　UpdateFrame / RenderFrame の関数ポインタと、そこへ渡す持ち物一式を組む。
		//------------------------------------------------------------------------
		FrameLoopContext loopContext{};
		loopContext.application      = &application;
		loopContext.device           = &device;
		loopContext.window           = &window;
		loopContext.jobSystem        = &jobSystem;
		loopContext.renderGraph      = &renderGraph;
		loopContext.sceneRenderer    = &sceneRenderer;
		loopContext.unlitRenderer    = &unlitRenderer;
		loopContext.renderStatistics = &renderStatistics;
#if FANG_ENABLE_DEBUG_DRAW
		loopContext.debugDraw = &debugDraw;
#endif
		loopContext.terrainRenderer = &terrainRenderer;

		FramePipeline framePipeline;
		if (!framePipeline.Initialize(jobSystem, frameMemory, &loopContext, &UpdateFrame, &RenderFrame))
		{
			FANG_FATAL("フレームパイプラインを組めなかった");
		}

		//------------------------------------------------------------------------
		// 7. 上の層(ゲーム / エディタ)の初期化
		// 　エンジン側の参照一式を EngineContext に束ねて渡す。
		//------------------------------------------------------------------------
		// 実時間の測定元。フレームループが Start してから毎周 Tick する。context より長く生きる必要がある。
		FrameClock frameClock;

		// 予算はフレームループが毎周更新し、エディタが読み書きする。context より長く生きる必要がある。
		PlatformBudget platformBudget;

		// 地形を読めていないときだけ nullptr。理由はここで 1 行だけ出す（毎フレームの経路にログを置かない）。
		const HeightmapTerrain* terrainForContext = terrain.isLoaded ? &terrain.heightmap : nullptr;
		if (terrainForContext == nullptr)
		{
			FANG_LOG_WARNING(Runtime, "地形が無い。接地は行われない");
		}

		// 全部の初期化が終わってから束ねる。上の層はここで受けた参照を持ち続ける。
#if FANG_ENABLE_HOT_RELOAD
		const EngineContext context{ jobSystem,
									 frameMemory,
									 framePipeline,
									 frameClock,
									 platformBudget,
									 meshRenderer,
									 hasCollisionWorld ? &collisionWorld : nullptr,
									 terrainForContext,
									 &device.GetShaderReloadStatus() };
#else
		const EngineContext context{
			jobSystem,
			frameMemory,
			framePipeline,
			frameClock,
			platformBudget,
			meshRenderer,
			hasCollisionWorld ? &collisionWorld : nullptr,
			terrainForContext,
		};
#endif
		if (!application.OnInitialize(context, device, window))
		{
			FANG_FATAL("上の層の初期化に失敗した");
		}

		//------------------------------------------------------------------------
		// 8. フレームループ
		// 　経過時間を測り、ウィンドウが閉じられるかデバイスロストまで RunFrame を回す。
		//------------------------------------------------------------------------
		FANG_LOG_INFO(Runtime, "フレームループを開始");

		// 1 周目に描く相手を作っておく。
		framePipeline.Prime();

		frameClock.Start();

		// ウィンドウを閉じるまでループする。WM_QUIT を受け取ると PumpMessages() が false を返す。
		while (!loopContext.hasDeviceError && window.PumpMessages())
		{
			// 実時間を測り、上限を掛ける。更新と描画のどちらにも同じ FrameTime を渡す。
			const FrameTime frameTime = frameClock.Tick();

			// ReadGamepadState はメインスレッドのみなので、更新ジョブへ投げる前にここで読む。
			const GamepadState gamepad = ReadGamepadState();

			const auto frameWorkBeginTime = std::chrono::steady_clock::now();

			framePipeline.RunFrame(frameTime, gamepad);

			// つまみが控えた値をここで実体へ入れる。RunFrame は末尾で更新ジョブを回収済みで、次の Submit まで
			// 調整値を読む相手が 1 人も走っていない ➡ 新しい同期を 1 つも足さずに競合が消える。
			// 効き始めは次の Submit から（最大 1 フレーム遅れ）。Release では控え帳が常に空。
			FANG_ASSERT(framePipeline.IsUpdateComplete(), "更新ジョブが走っている間に調整値を書こうとしている");
			(void)TuningRegistry::GetInstance().ApplyPendingWrites();

			// 予算の判定と、制限が入っているときの待ちはここで行う。
			// 待った分は次の周の刻みに乗るので、実処理の時間だけを渡す。
			const float frameWorkSeconds =
				std::chrono::duration<float>(std::chrono::steady_clock::now() - frameWorkBeginTime).count();
			platformBudget.EndFrame(frameWorkSeconds);
		}

		FANG_LOG_INFO(
			Runtime,
			"フレームループを終了（上限で切った周: {} / 経過 {:.1f} 秒）",
			frameClock.GetClampedFrameCount(),
			frameClock.GetElapsedSeconds()
		);

		//------------------------------------------------------------------------
		// 9. 終了処理
		// 　作った順の逆に畳む。上の層 ➡ レンダラ ➡ デバイス ➡ ウィンドウ ➡ 土台。
		//------------------------------------------------------------------------
		framePipeline.Shutdown();
		application.OnShutdown(device);
		collisionWorld.Shutdown();
		unlitRenderer.Shutdown(device);
#if FANG_ENABLE_DEBUG_DRAW
		debugDraw.Shutdown(device);
#endif
		sceneRenderer.Shutdown(device);
		meshRenderer.Shutdown(device); // Game が読み込んだメッシュもまとめてここで解放される。
		terrainRenderer.Shutdown(device);

		// 地形のテクスチャは TerrainRenderer にとって借用なので、持ち主のここが返す。
		device.DestroyTexture(terrain.splatmap);
		for (rhi::TextureHandle& layerAlbedo : terrain.layerAlbedos)
		{
			device.DestroyTexture(layerAlbedo);
		}

		for (rhi::TextureHandle& layerNormal : terrain.layerNormals)
		{
			device.DestroyTexture(layerNormal);
		}

		device.Shutdown();
		window.Shutdown();

		// フレームメモリはジョブから触られるので、ワーカーを畳んでから返す。
		jobSystem.Shutdown();
		frameMemory.Shutdown();

		return 0;
	}
} // namespace fang
