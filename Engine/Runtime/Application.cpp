/**
 * @file Application.cpp
 * @brief フレームループと初期化順。
 */
#include "Pch.h"
#include "Runtime/Application.h"
#include "Animation/AnimationPlayback.h"
#include "Animation/SkeletalAnimation.h"
#include "Core/Job/JobSystem.h"
#include "Core/Log/Assert.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/AssetPath.h"
#include "Core/Platform/Window.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/DebugDraw.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/TerrainRenderer.h"
#include "Renderer/UnlitRenderer.h"
#include "Resource/DdsImage.h"
#include "Resource/GltfMesh.h"
#include "Resource/GltfScene.h"
#include "Resource/HeightmapTerrain.h"
#include "Runtime/FramePipeline.h"
#include "Runtime/RuntimeLog.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>


FANG_DEFINE_LOG_CATEGORY(Runtime);


namespace fang
{
	namespace
	{
		constexpr rhi::ClearColor BACKGROUND_COLOR{ 0.05f, 0.06f, 0.09f, 1.0f };

		/** @brief 狼の glTF。アセットの根っこからの相対パス。 */
		constexpr const char* WOLF_MODEL_RELATIVE_PATH = "Models\\Wolf.gltf";

		// 骨とクリップは gltf2ozz が Wolf.gltf から出したもの。歩き（直進）はルートモーションを持たないので、
		// その場で脚だけが動く ➡ 移動処理がまだ無くても再生の正しさが確かめられる。
		constexpr const char* WOLF_SKELETON_RELATIVE_PATH = "Models\\WolfSkeleton.ozz";
		constexpr const char* WOLF_CLIP_RELATIVE_PATH     = "Models\\A_WalkSlow_F.ozz";

		/** @brief ステージの glTF。アセットの根っこからの相対パス。 */
		constexpr const char* STAGE_MODEL_RELATIVE_PATH = "Models\\Stage.gltf";

		/** @brief 狼とステージ、どちらの glTF も置く場所。テクスチャの相対パスの組み立てに使う。 */
		constexpr const char* MODEL_FOLDER_RELATIVE_PATH = "Models\\";

		/** @brief 恒久の RenderItem 配列のうち、毎フレーム書き換える動く席の数（狼 2 体）。 */
		constexpr size_t DYNAMIC_RENDER_ITEM_COUNT = 2;

		/**
		 * @brief ステージの配置数の上限。
		 * @details 恒久配列は MeshRenderer::MAX_ITEM_COUNT ぶん確保する（1 フレームに描ける数と揃えないと、
		 *          ステージだけで b0 の置き場を使い切って狼が描けなくなる）。そのうち動く 2 席を
		 *          引いた残りがステージの取り分。
		 */
		constexpr size_t MAX_STAGE_INSTANCE_COUNT = MeshRenderer::MAX_ITEM_COUNT - DYNAMIC_RENDER_ITEM_COUNT;

		// 狼の頂点の実測範囲は X[-92.6, 111.4]（体長 204）、Y[-0.39, 106.6]（高さ 107）、Z[±18.2]（幅 36）。
		// 単位は 1 = 1cm。狼は X 軸に沿って立っているので、真横に当たる Z 方向から見るのが素直。
		// 注視点は 2 体の中点（接地後の高さ）へ毎フレーム足す ➡ 狼が上下しても画の中でずれない。
		constexpr Vector3 CAMERA_TARGET_OFFSET{ 9.4f, 53.0f, 0.0f };
		constexpr Vector3 CAMERA_UP{ 0.0f, 1.0f, 0.0f };

		/** @brief 垂直画角。ラジアン。 */
		constexpr float CAMERA_FIELD_OF_VIEW_Y_RADIANS = 60.0f * PI / 180.0f;

		// 注視点からカメラまでの距離。カメラは一周するので、どの角度でも 2 体とも収まる距離が要る。
		// 2 体は Z 方向へ 1150 離れていて体の張り出しは片側 111 ➡ 収めたい半径は 1150 / 2 + 111 = 686。
		// 16:9 なので tan(横半角) = (16/9) * tan(30 度) = 1.026 ➡ 横半角 45.7 度。686 / sin(45.7 度) = 959。
		// これに少し余白を足した値にする。
		constexpr float CAMERA_DISTANCE = 1000.0f;

		// カメラの俯角。水平のままだと視点の高さが 98 で、周回する円（水平半径 940）上の地表 195 に潜る。
		// 20 度なら視点が 440 まで上がり、地表との間に 245 の余裕が残る。
		constexpr float CAMERA_PITCH_RADIANS = 20.0f * PI / 180.0f;

		// 近平面・遠平面。狼が 100〜200 単位なので、0.1 のような近さに置くと深度の精度を捨てることになる。
		// 手前の面が奥を隠しているかを見るのが目的なので、手前の狼までの距離より内側にあれば足りる。
		// 遠平面は、視点が原点から最大 1600 離れるのに対し地形が原点から対角 5792 まで広がる ➡ 端まで映る値。
		constexpr float CAMERA_NEAR_Z = 10.0f;
		constexpr float CAMERA_FAR_Z  = 8000.0f;

		/** @brief カメラが狼の周りを 1 周する秒数。 */
		constexpr float CAMERA_ORBIT_SECONDS = 20.0f;

		// 狼 2 体のワールド XZ。Y は毎フレーム地表から決めるので持たない。
		// 1 体目はクリアリング（半径 800 の平地、地表 12.0）の中心。2 体目はその外へ出して、高さの違う
		// 2 点で正しく載ることが 1 枚の画で見えるようにする（地表 78.3 ➡ 1 体目より 66.3 高い）。
		// (0, 1150) は体の前後（X 方向 -92.6〜+111）で地表差が 0.1、左右（Z 方向 ±18.2）でも 11.5 しか
		// 無いので、脚 IK がまだ無くても 4 本の脚が地表から ±6 に収まる。
		/** @brief 狼 2 体を置くワールド XZ。要素数を動く席の数に縛ってある。 */
		constexpr std::array<Vector3, DYNAMIC_RENDER_ITEM_COUNT> WOLF_POSITIONS{
			Vector3{ 0.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1150.0f },
		};

#if FANG_ENABLE_DEBUG_DRAW
		/** @brief RenderItem の境界ボックスを表す線の色。緑系にして他の要素と見分けやすくする。 */
		constexpr Vector3 DEBUG_DRAW_BOUNDS_COLOR{ 0.0f, 1.0f, 0.3f };

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

		constexpr float TERRAIN_TOTAL_SIZE_CENTIMETERS   = 8192.0f;
		constexpr float TERRAIN_HEIGHT_SCALE_CENTIMETERS = 600.0f;

		/** @brief レイヤのテクスチャ 1 枚が受け持つ辺長。4m ごとの繰り返しなら近接しても粗さが目立たない。 */
		constexpr float TERRAIN_LAYER_TILE_CENTIMETERS = 400.0f;

		// 地形は静的なので設定値はここに直書きする(実行時に変える口を増やさない)。
		// パラメータの JSON 化はエディタのシーン保存と一緒に行う。
		/** @brief レイヤごとの知覚 roughness。並びは草・岩・土。岩だけ少しハイライトを残す。 */
		constexpr float TERRAIN_LAYER_ROUGHNESS[3] = { 0.9f, 0.75f, 0.95f };

		/**
		 * @brief 狼 1 体ぶんの持ち物。
		 * @details Scene ができたらゲーム側のオブジェクトへ移る。今はフレームループが直接抱えている。
		 */
		struct WolfModel
		{
			/** @brief GPU に載ったメッシュ。読み込みに失敗すると無効なままで、そのときは描画を飛ばす。 */
			MeshId mesh;

			/** @brief 骨を持つメッシュとして読めたか。false なら静的メッシュとして描く。 */
			bool isSkinned = false;

			SkeletalAnimation animation;
			AnimationPlayback playback;

			/** @brief ベースカラー。読めなかったら無効なままで、レンダラがダミー（単色）を差す。 */
			rhi::TextureHandle baseColor;

			// マテリアルの係数。読み込みのときに glTF から写す。狼は metallic 0（非金属）・roughness 1（粗い面）。
			float metallicFactor  = 0.0f;
			float roughnessFactor = 1.0f;

			/** @brief バインドポーズを打ち消す行列。glTF の関節の並び。読み込みのときだけ確保する。 */
			std::vector<Matrix4x4> inverseBindMatrices;

			/**
			 * @brief 毎フレーム作り直すスキニング行列。
			 * @details 単位行列で初期化してあるので、クリップを読めていなくてもバインドポーズで描ける。
			 *          置き場は読み込みのときに取り切る ➡ 毎フレームのヒープ確保は 0。
			 */
			std::vector<Matrix4x4> skinningMatrices;
		};

		/**
		 * @brief 地形 1 式の持ち物。
		 * @details HeightmapTerrain は高さの問い合わせ(狼の接地に使う予定)のため、GPU 化が済んでも持ち続ける。
		 */
		struct TerrainModel
		{
			HeightmapTerrain heightmap;

			/** @brief スプラットマップ。読めなかったら無効なままで、そのとき地形は描かれない。 */
			rhi::TextureHandle splatmap;

			/** @brief レイヤのアルベド。並びは草・岩・土。 */
			rhi::TextureHandle layerAlbedos[3];

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
		 *          (狼・エディタは今までどおり)。理由は各段階がログに出す。
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


		/**
		 * @brief ステージ 1 つぶんの持ち物。
		 * @details renderItems は先頭 DYNAMIC_RENDER_ITEM_COUNT 個を狼の動く席として空けたまま返す
		 *          （中身は既定の無効な RenderItem。RenderFrame が毎フレーム上書きする）。3 番目以降は
		 *          LoadStage が書いたきり動かない。meshes と textures は終了処理での解放にだけ使う。
		 */
		struct StageResources
		{
			std::vector<RenderItem>         renderItems;
			std::vector<MeshId>             meshes;
			std::vector<rhi::TextureHandle> textures;
		};

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
			MeshRenderer*    meshRenderer    = nullptr;
			TerrainRenderer* terrainRenderer = nullptr;
			WolfModel*       wolf            = nullptr;

			/** @brief 狼の足元の高さの問い合わせ先。地形を読めていなければ nullptr ➡ 接地せず y = 0 に立つ。 */
			const HeightmapTerrain* terrain = nullptr;

			/** @brief RunApplication が持つ入れ物。graph.Execute の戻り値の後に 4 値を書く。 */
			RenderStatistics* renderStatistics = nullptr;

			/**
			 * @brief 恒久の RenderItem 配列。ロード時に resize し切り、以後は要素数を変えない。
			 * @details 先頭 DYNAMIC_RENDER_ITEM_COUNT 個（狼 2 体）は毎フレーム書き換える動く席。
			 *          3 番目以降はステージの配置で、ロード時に書いたきり動かない。Submit はこの配列
			 *          全体を指す span 1 本で済ませる ➡ 毎フレームのヒープ確保・vector 伸長は無い。
			 */
			std::vector<RenderItem> renderItems;

			/** @brief ステージのメッシュ番号。終了処理では使わない（MeshRenderer::Shutdown がまとめて解放する）。 */
			std::vector<MeshId> stageMeshes;

			/** @brief ステージのベースカラー。終了処理でまとめて DestroyTexture するために持つ。 */
			std::vector<rhi::TextureHandle> stageTextures;

			/** @brief カメラの水平回転角（ラジアン）。入力の仕組みがまだ無いので時間で回す。 */
			float cameraOrbitRadians = 0.0f;

			/** @brief バックバッファを取れなかったフレームで立つ。ループを抜ける合図。 */
			bool hasDeviceError = false;
		};


		/**
		 * @brief glTF が指す画像パスから、実際に読む .dds の絶対パスを作る。
		 * @details 画像は texconv でオフライン変換してある ➡ glTF は .png を指したままなので、
		 *          拡張子をここで差し替える。区切りの / も \ へ直す。folder はアセットの根っこからの
		 *          相対パス（例: MODEL_FOLDER_RELATIVE_PATH）で、狼とステージのどちらも同じ手順を通す。
		 */
		[[nodiscard]] std::string MakeModelTexturePath(
			std::string_view modelFolderRelativePath,
			std::string_view imagePath
		)
		{
			std::string relativePath(modelFolderRelativePath);
			relativePath += imagePath;

			for (char& character : relativePath)
			{
				if (character == '/')
				{
					character = '\\';
				}
			}

			const size_t dotIndex = relativePath.rfind('.');
			if (dotIndex != std::string::npos)
			{
				relativePath.resize(dotIndex);
			}
			relativePath += ".dds";

			return MakeAssetPath(relativePath.c_str());
		}

		/**
		 * @brief 狼のベースカラーを読んで GPU へ載せる。
		 * @details 失敗しても落とさない。無効なハンドルのままなら、レンダラがダミー（単色）を差す。
		 */
		[[nodiscard]] rhi::TextureHandle LoadWolfBaseColor(rhi::GraphicsDevice& device, const GltfMesh& model)
		{
			if (model.GetBaseColorImagePath().empty())
			{
				return rhi::TextureHandle{};
			}

			// DdsImage は転送が済めば用済み。5MB の中身をこの関数を抜けるところで手放す。
			const std::string filePath =
				MakeModelTexturePath(MODEL_FOLDER_RELATIVE_PATH, model.GetBaseColorImagePath());

			DdsImage image;
			if (!image.Load(filePath.c_str()))
			{
				FANG_LOG_ERROR(Runtime, "狼のベースカラーを読めなかった。単色で描く: {}", filePath);
				return rhi::TextureHandle{};
			}

			const rhi::TextureSource source{
				.mipLevels = image.GetMipLevels(),
				.format    = image.GetFormat(),
			};

			// 失敗したときの理由は RHI 側がログに出す。
			return device.CreateTexture2D(source);
		}

		/**
		 * @brief 骨とクリップを読み、姿勢を作れる状態にする。
		 * @details 失敗しても落とさない。IsReady() が false のままになり、狼はバインドポーズで立つ。
		 */
		void LoadWolfAnimation(const GltfMesh& model, WolfModel* outWolf)
		{
			const std::string skeletonPath = MakeAssetPath(WOLF_SKELETON_RELATIVE_PATH);
			if (!outWolf->animation.LoadSkeleton(skeletonPath.c_str()))
			{
				FANG_LOG_ERROR(Runtime, "狼のスケルトンを読めなかった。バインドポーズで描く: {}", skeletonPath);
				return;
			}

			const std::string clipPath = MakeAssetPath(WOLF_CLIP_RELATIVE_PATH);
			if (!outWolf->animation.LoadClip(clipPath.c_str()))
			{
				FANG_LOG_ERROR(Runtime, "狼のクリップを読めなかった。バインドポーズで描く: {}", clipPath);
				return;
			}

			// gltf2ozz は関節を並べ替える。名前で対応表を作らないと、骨の対応がずれた姿勢が描かれる。
			if (!outWolf->animation.BuildJointRemap(model.GetJointNames()))
			{
				FANG_LOG_ERROR(Runtime, "狼の関節の対応表を作れなかった。バインドポーズで描く");
				return;
			}

			outWolf->playback.SetDurationSeconds(outWolf->animation.GetClipDurationSeconds());

			FANG_LOG_INFO(Runtime, "狼のアニメーションを読んだ: {:.3f} 秒", outWolf->playback.GetDurationSeconds());
		}

		/**
		 * @brief 狼のモデルを読んで GPU へ載せる。骨を持っていればアニメーションも読む。
		 * @details 失敗しても落とさない。モデルが出なくても三角形とエディタは動き、ゲームの本質でもないため。
		 */
		void LoadWolf(rhi::GraphicsDevice& device, MeshRenderer& meshRenderer, WolfModel* outWolf)
		{
			//------------------------------------------------------------------------
			// 1. glTF の読み込み
			// 　頂点・骨・マテリアル・画像パスの入れ物である glTF ファイルを 1 つ読み込む。読めなければ
			// 　ここで引き返し、モデルは出さない。
			//------------------------------------------------------------------------
			// GltfMesh は CreateMesh が済めば用済み。15MB の .bin 由来の配列を抱え続けないよう、
			// この関数を抜けるところで手放す。逆バインド行列と関節名だけは写しを残す。
			GltfMesh model;

			const std::string filePath = MakeAssetPath(WOLF_MODEL_RELATIVE_PATH);
			if (!model.Load(filePath.c_str()))
			{
				FANG_LOG_ERROR(Runtime, "狼のモデルを読めなかった: {}", filePath);
				return;
			}

			//------------------------------------------------------------------------
			// 2. 頂点・骨・テクスチャの取り出しと GPU リソース化
			// 　ベースカラーのテクスチャとマテリアル係数を取り出したあと、骨の有無で分岐する。骨が無ければ
			// 　頂点(位置・法線・UV)だけを取り出して CreateMesh で静的メッシュとして GPU へ載せる。骨が
			// 　あれば関節番号・重みも合わせて取り出してスキンメッシュとして載せ、続けて逆バインド行列を
			// 　写し、LoadWolfAnimation でスケルトンとクリップを読む。
			//------------------------------------------------------------------------
			outWolf->baseColor = LoadWolfBaseColor(device, model);

			outWolf->metallicFactor  = model.GetMetallicFactor();
			outWolf->roughnessFactor = model.GetRoughnessFactor();

			if (!model.HasSkin())
			{
				// 骨を持たない glTF なら静的メッシュとして描く。失敗の理由は CreateMesh 側がログに出す。
				const MeshSource source{
					.positions = model.GetPositions(),
					.normals   = model.GetNormals(),
					.texCoords = model.GetTexCoords(),
					.indices   = model.GetIndices(),
				};

				outWolf->mesh = meshRenderer.CreateMesh(device, source);
				return;
			}

			const SkinnedMeshSource source{
				.positions    = model.GetPositions(),
				.normals      = model.GetNormals(),
				.texCoords    = model.GetTexCoords(),
				.indices      = model.GetIndices(),
				.jointIndices = model.GetJointIndices(),
				.jointWeights = model.GetJointWeights(),
			};

			outWolf->mesh = meshRenderer.CreateMesh(device, source);
			if (!outWolf->mesh.IsValid())
			{
				return;
			}

			outWolf->isSkinned = true;

			const std::span<const Matrix4x4> inverseBindMatrices = model.GetInverseBindMatrices();
			outWolf->inverseBindMatrices.assign(inverseBindMatrices.begin(), inverseBindMatrices.end());

			// 単位行列のまま置いておく ➡ クリップを読めなくてもバインドポーズが出る。
			outWolf->skinningMatrices.resize(inverseBindMatrices.size());

			LoadWolfAnimation(model, outWolf);
		}

		/**
		 * @brief ステージを読み、メッシュを GPU へ載せ、地表へ接地させた恒久の RenderItem 配列を作り切る。
		 * @details 失敗しても FANG_FATAL にしない。読めなければ舞台なしで起動を続ける（ログだけ出す）。
		 *          先頭 DYNAMIC_RENDER_ITEM_COUNT 個は狼の動く席として空けたまま返す ➡ 呼び出し側
		 *          （RunApplication）は成否によらずこの関数を抜けた時点で renderItems を Submit に使える。
		 * @param terrain 接地に使う地形。読めていなければ nullptr を渡す ➡ 配置は glTF のまま y = 0 に置く。
		 */
		void LoadStage(
			rhi::GraphicsDevice&    device,
			MeshRenderer&           meshRenderer,
			const HeightmapTerrain* terrain,
			StageResources*         outStage
		)
		{
			GltfScene scene;

			const std::string filePath = MakeAssetPath(STAGE_MODEL_RELATIVE_PATH);
			if (!scene.Load(filePath.c_str()))
			{
				FANG_LOG_ERROR(Runtime, "ステージを読めなかった。舞台なしで続ける: {}", filePath);
				return;
			}

			const std::span<const GltfSceneMesh>     meshes    = scene.GetMeshes();
			const std::span<const GltfSceneInstance> instances = scene.GetInstances();

			//------------------------------------------------------------------------
			// 1. メッシュごとに GPU へ載せる
			// 　失敗した番号は無効なまま outStage->meshes へ残す ➡ MeshRenderer::Draw / DrawDepth が
			// 　黙って飛ばすので、ここで詰め直す必要が無い。
			//------------------------------------------------------------------------
			outStage->meshes.reserve(meshes.size());
			for (const GltfSceneMesh& mesh : meshes)
			{
				const MeshSource source{
					.positions = mesh.positions,
					.normals   = mesh.normals,
					.texCoords = mesh.texCoords,
					.indices   = mesh.indices,
				};
				outStage->meshes.push_back(meshRenderer.CreateMesh(device, source));
			}

			//------------------------------------------------------------------------
			// 2. 画像パスごとにテクスチャを読む
			// 　同じパスを指すメッシュが並ぶので、パスごとに 1 回だけ読む対応表を挟む。数十件なので
			// 　線形探索で足りる(二分探索や辞書を持ち出すほどの件数ではない)。
			//------------------------------------------------------------------------
			std::vector<std::pair<std::string, rhi::TextureHandle>> pathToTexture;

			auto findOrLoadTexture = [&](std::string_view imagePath) -> rhi::TextureHandle {
				if (imagePath.empty())
				{
					return rhi::TextureHandle{};
				}

				for (const auto& entry : pathToTexture)
				{
					if (entry.first == imagePath)
					{
						return entry.second;
					}
				}

				const std::string imageFilePath = MakeModelTexturePath(MODEL_FOLDER_RELATIVE_PATH, imagePath);

				rhi::TextureHandle handle;
				DdsImage           image;
				if (image.Load(imageFilePath.c_str()))
				{
					const rhi::TextureSource textureSource{
						.mipLevels = image.GetMipLevels(),
						.format    = image.GetFormat(),
					};

					handle = device.CreateTexture2D(textureSource);
					if (handle.IsValid())
					{
						outStage->textures.push_back(handle);
					}
				}
				else
				{
					FANG_LOG_ERROR(Runtime, "ステージのベースカラーを読めなかった。単色で描く: {}", imageFilePath);
				}

				// 失敗した画像パスも登録しておく（無効なハンドルのまま）。同じ壊れたパスを指す次のメッシュで
				// また読みに行って同じ理由のログを重ねて出す、ということを避けるため。
				pathToTexture.emplace_back(std::string(imagePath), handle);
				return handle;
			};

			//------------------------------------------------------------------------
			// 3. 配置ごとに地表へ載せ、RenderItem を恒久配列(3 番目以降)へ積む
			// 　上限(MAX_STAGE_INSTANCE_COUNT)を超えたぶんは警告して捨てる。castsShadow は常に false
			// 　(ステージは受け専用。光の箱はキャスタの AABB の和なので、含めると光源から見える範囲が
			// 　無駄に広がり、シャドウマップ 1 テクセルが表す実寸が粗くなって狼の影が読めなくなる)。
			//------------------------------------------------------------------------
			const size_t loadedInstanceCount = std::min(instances.size(), MAX_STAGE_INSTANCE_COUNT);
			outStage->renderItems.resize(DYNAMIC_RENDER_ITEM_COUNT + loadedInstanceCount);

			size_t      groundedInstanceCount = 0;
			size_t      outsideInstanceCount  = 0;
			std::string firstOutsideName;

			for (size_t index = 0; index < loadedInstanceCount; ++index)
			{
				const GltfSceneInstance& instance = instances[index];
				const GltfSceneMesh&     meshData = meshes[instance.meshIndex];
				const MeshId             meshId   = outStage->meshes[instance.meshIndex];

				// ステージの glTF はどの組み立ても最下段の底面がローカル y = 0 にそろえてある
				// ➡ 原点 XZ の地表の高さを Y へ「足す」だけで底が地表に付く(代入すると、原点が底面でない
				// 　井戸のようなメッシュがめり込む)。回転・スケールには触らない。
				Matrix4x4 world = instance.world;
				if (terrain != nullptr)
				{
					float groundHeight = 0.0f;
					if (terrain->TryGetHeightAt(world.m[3][0], world.m[3][2], &groundHeight))
					{
						world.m[3][1] += groundHeight;
						++groundedInstanceCount;
					}
					else
					{
						// 地形の外。端の高さへ寄せると崖に貼り付いて見えるので、そのまま動かさない。
						if (outsideInstanceCount == 0)
						{
							firstOutsideName = instance.name;
						}
						++outsideInstanceCount;
					}
				}

				RenderItem item{};
				item.mesh            = meshId;
				item.world           = world;
				item.baseColor       = findOrLoadTexture(meshData.baseColorImagePath);
				item.metallicFactor  = meshData.metallicFactor;
				item.roughnessFactor = meshData.roughnessFactor;
				item.castsShadow     = false;

				// bounds は無効な mesh では作れない（TransformAabb は有効な箱を要求する）。無効なままにする
				// と「常に描く」扱いになるが、mesh 自体を MeshRenderer が飛ばすので実害は無い。
				// 接地後の world から作る ➡ カリングと影の箱がずれたまま残らない。
				if (meshId.IsValid())
				{
					item.bounds = TransformAabb(meshRenderer.GetLocalBounds(meshId), world);
				}

				outStage->renderItems[DYNAMIC_RENDER_ITEM_COUNT + index] = item;
			}

			const size_t discardedInstanceCount = instances.size() - loadedInstanceCount;
			if (discardedInstanceCount > 0)
			{
				FANG_LOG_WARNING(
					Runtime,
					"ステージの配置が上限（{}）を超えた。{} 個を捨てた",
					MAX_STAGE_INSTANCE_COUNT,
					discardedInstanceCount
				);
			}

			FANG_LOG_INFO(
				Runtime,
				"ステージを読んだ: メッシュ {} 個 / 配置 {} 個 / テクスチャ {} 枚",
				outStage->meshes.size(),
				loadedInstanceCount,
				outStage->textures.size()
			);

			// 接地の結果は必ず 1 行残す。浮いている・沈んでいるように見えたとき、理由をログで切り分けられる。
			if (terrain == nullptr)
			{
				FANG_LOG_WARNING(Runtime, "地形が無いので接地しない。ステージは y=0 のまま");
			}
			else
			{
				FANG_LOG_INFO(Runtime, "接地 {} 個 / 範囲外 {} 個", groundedInstanceCount, outsideInstanceCount);

				if (outsideInstanceCount > 0)
				{
					FANG_LOG_WARNING(
						Runtime,
						"地形の範囲外の配置は接地しない: {} 個（最初は {}）",
						outsideInstanceCount,
						firstOutsideName
					);
				}
			}
		}

		/**
		 * @brief 再生位置を進め、そのフレームのスキニング行列を作る。
		 * @details 姿勢を作れないときは行列を触らない ➡ 単位行列のままバインドポーズで描かれる。
		 */
		void UpdateWolfPose(WolfModel* wolf, float deltaTimeSeconds)
		{
			if (!wolf->animation.IsReady())
			{
				return;
			}

			wolf->playback.Advance(deltaTimeSeconds);

			FANG_VERIFY(wolf->animation.ComputeSkinningMatrices(
				wolf->playback.GetTimeRatio(),
				wolf->inverseBindMatrices,
				wolf->skinningMatrices
			));
		}

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
			auto& loopContext = *static_cast<FrameLoopContext*>(userData);

			rhi::GraphicsDevice& device        = *loopContext.device;
			Window&              window        = *loopContext.window;
			JobSystem&           jobSystem     = *loopContext.jobSystem;
			RenderGraph&         graph         = *loopContext.renderGraph;
			SceneRenderer&       sceneRenderer = *loopContext.sceneRenderer;

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
			// 3. 狼の足元の高さと View の組み立て(時間で回るカメラ)
			// 　SceneRenderer::Reset で前フレームの View を捨ててから、カメラ位置と視射影行列を計算して View を
			// 　組み立てる。登録(AddShadowView / AddView)はキャスタの箱がそろう次の区画でまとめて行う。
			// 　入力の仕組みがまだ無いので、経過時間だけでカメラを狼の周りに回す。
			// 　地表の高さは注視点にも狼の world にも要る ➡ 区画 4 より先にここで引いて、両方で使い回す。
			//------------------------------------------------------------------------
			sceneRenderer.Reset();

			// 入力の仕組みがまだ無いので、時間でカメラを回して全方向から形と前後関係を確かめられるようにする。
			// 狼を読めているかによらず視点は要る（View が無いと ScenePass が画面をクリアできない）。
			loopContext.cameraOrbitRadians += deltaTimeSeconds * (2.0f * PI / CAMERA_ORBIT_SECONDS);
			if (loopContext.cameraOrbitRadians >= 2.0f * PI)
			{
				// 積みっぱなしにすると値が大きくなるほど角度の刻みが粗くなるので、1 周ごとに戻す。
				loopContext.cameraOrbitRadians -= 2.0f * PI;
			}

			// 狼 2 体の足元の地表の高さ。地形を読めていなければ 0 のままで、狼もカメラも y = 0 基準に戻る。
			// GetHeightAt は範囲外を端の高さへクランプする ➡ 狼が地形の外へ出ても高さが未定義にならない。
			float   groundHeights[DYNAMIC_RENDER_ITEM_COUNT]{};
			Vector3 groundedCenter{};
			for (size_t index = 0; index < DYNAMIC_RENDER_ITEM_COUNT; ++index)
			{
				const Vector3& wolfPosition = WOLF_POSITIONS[index];
				if (loopContext.terrain != nullptr)
				{
					groundHeights[index] = loopContext.terrain->GetHeightAt(wolfPosition.x, wolfPosition.z);
				}

				groundedCenter += Vector3{ wolfPosition.x, wolfPosition.y + groundHeights[index], wolfPosition.z };
			}

			// 注視点は 2 体の中点（接地後の高さ）。狼が上下しても画の中でずれない。
			const Vector3 cameraTarget =
				groundedCenter * (1.0f / static_cast<float>(DYNAMIC_RENDER_ITEM_COUNT)) + CAMERA_TARGET_OFFSET;

			// カメラは俯角を付けた円錐面を周る。水平半径は距離 × cos(俯角)、高さは距離 × sin(俯角)。
			// 水平のままだと周回の途中で丘に潜るので、俯角で視点を持ち上げてある。
			const float   orbitRadius = CAMERA_DISTANCE * std::cosf(CAMERA_PITCH_RADIANS);
			const Vector3 orbitOffset{
				std::sinf(loopContext.cameraOrbitRadians) * orbitRadius,
				CAMERA_DISTANCE * std::sinf(CAMERA_PITCH_RADIANS),
				std::cosf(loopContext.cameraOrbitRadians) * orbitRadius,
			};
			const Vector3 eye = cameraTarget + orbitOffset;

			// 最小化すると幅も高さも 0 で来る。ゼロ除算と MakePerspectiveMatrix のアサートを避けて 1 で止める。
			const float viewportWidth  = static_cast<float>(window.GetWidth() > 0 ? window.GetWidth() : 1);
			const float viewportHeight = static_cast<float>(window.GetHeight() > 0 ? window.GetHeight() : 1);

			// 光は 1 つ前のフレームの更新が書いたもの。まだ何も書かれていなければ既定の光で描く
			// ➡ Game が光を書かなくても真っ黒にはならない。
			const DirectionalLight  defaultLight{};
			const DirectionalLight& light = frameData != nullptr ? frameData->light : defaultLight;

			const View view{
				.viewProjection = Multiply(
					MakeLookAtMatrix(eye, cameraTarget, CAMERA_UP),
					MakePerspectiveMatrix(
						CAMERA_FIELD_OF_VIEW_Y_RADIANS,
						viewportWidth / viewportHeight,
						CAMERA_NEAR_Z,
						CAMERA_FAR_Z
					)
				),
				.cameraPosition   = eye,
				.directionToLight = light.directionToLight,
				.lightColor       = light.color,
				.lightIntensity   = light.intensity,
				.ambientColor     = light.ambientColor,
			};

			//------------------------------------------------------------------------
			// 4. RenderItem 列の先頭 2 スロットの書き換え(狼 2 体)
			// 　loopContext.renderItems は起動時に resize し切った恒久配列。3 番目以降(ステージ)は
			// 　ロード時に書いたきりなのでここでは触らない。先頭 2 つだけ、読めていれば毎フレーム
			// 　world / bounds / skinningMatrices を書き直す(読めていなければ既定の無効な RenderItem
			// 　に戻す ➡ MeshRenderer が黙って飛ばすので、詰め直しの分岐が要らない)。
			//------------------------------------------------------------------------
			// loopContext は RenderFrame の外（RunApplication）が持つので、graph.Execute が戻るまで
			// 生きている。Submit はこの配列を指す span を控えるだけでコピーしない。
			std::vector<RenderItem>& renderItems = loopContext.renderItems;

			// 狼を描く。読めていなければメッシュの描画だけを飛ばし、ほかは今までどおり続ける。
			for (RenderItem& dynamicItem : std::span(renderItems).first(DYNAMIC_RENDER_ITEM_COUNT))
			{
				dynamicItem = RenderItem{};
			}

			WolfModel& wolf = *loopContext.wolf;
			if (wolf.mesh.IsValid())
			{
				if (wolf.isSkinned)
				{
					// 再生位置を進めるのはここ。カメラの回転と同じ場所に置いてある
					// ➡ Scene ができたらカメラごとゲーム側の更新へ移る。
					UpdateWolfPose(&wolf, deltaTimeSeconds);
				}

				const Aabb localBounds = loopContext.meshRenderer->GetLocalBounds(wolf.mesh);

				// 2 体とも同じ骨行列（span を共有）で描くので、同じポーズで歩いて見える。
				for (size_t index = 0; index < DYNAMIC_RENDER_ITEM_COUNT; ++index)
				{
					// 狼の足裏はローカル y = 0 ➡ 地表の高さを Y へ足すだけで接地する（ステージの glTF と
					// 同じ規約）。bounds は接地後の world から作るので、箱が地面に取り残されない。
					const Vector3& wolfPosition = WOLF_POSITIONS[index];

					Matrix4x4 world{};
					world.m[3][0] = wolfPosition.x;
					world.m[3][1] = wolfPosition.y + groundHeights[index];
					world.m[3][2] = wolfPosition.z;

					renderItems[index] = RenderItem{
						.mesh             = wolf.mesh,
						.world            = world,
						.bounds           = TransformAabb(localBounds, world),
						.skinningMatrices = wolf.skinningMatrices,
						.baseColor        = wolf.baseColor,
						.metallicFactor   = wolf.metallicFactor,
						.roughnessFactor  = wolf.roughnessFactor,
					};
				}
			}

			const std::span<const RenderItem> submittedItems(renderItems);

			//------------------------------------------------------------------------
			// 5. シャドウ View とシーン View の登録・Submit
			// 　castsShadow かつ bounds が有効な RenderItem を union してキャスタの箱を作り、AddShadowView を
			// 　AddView より先に呼ぶ(シーン View の b1 に光の行列を焼き込む契約のため)。同じ RenderItem 列を
			// 　両方の View へ Submit する(span の実体は上の items 配列で、Execute が戻るまで生きている)。
			//------------------------------------------------------------------------
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
			// 6. Unlit パスの宣言(クリアを持つ・三角形が先の理由は既存コメントにある)
			// 　三角形を描く UnlitPass を宣言する。バックバッファと深度、両方のクリアをこのパスが引き受ける。
			//------------------------------------------------------------------------
			// 三角形を先に描き、深度を持つ狼が上に乗る従来の前後関係を保つ。三角形は深度テストを持たないので、
			// 狼より後に描くと三角形が常に上書きしてしまう ➡ 画面のクリアもこのパスが引き受ける。
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
			// 7. ScenePass の宣言(Load)
			// 　床と狼を描く ScenePass を View ごとに宣言する。
			//------------------------------------------------------------------------
			// 床と狼（1 体〜2 体）を描く ScenePass を View ごとに宣言する。三角形パスが画面をクリア済みなので、
			// 最初の View も Load（前のパスが描いた画の上に重ねる）。
			sceneRenderer.AddPasses(
				graph,
				backBufferResource,
				depthBufferResource,
				shadowMapResource,
				BACKGROUND_COLOR,
				EnLoadOperation::Load
			);

			//------------------------------------------------------------------------
			// 8. TerrainPass の宣言(Load・シャドウマップ読み)
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
			// 9. デバッグ描画の積み込みと DebugLinePass の宣言(FANG_ENABLE_DEBUG_DRAW 内)
			// 　Reset で前フレームの積み込みを捨ててから、狼(と床)の境界ボックス、シャドウの光の視錐台の順に
			// 　ワイヤーを積み、DebugLinePass を宣言する。Release では FANG_ENABLE_DEBUG_DRAW が 0 になり、
			// 　この区画ごとビルドから外れる。
			//------------------------------------------------------------------------
			DebugDraw& debugDraw = *loopContext.debugDraw;

			debugDraw.Reset();

			// 要件は狼 2 体の AABB のみが成功条件なので、床を除く判定は入れず bounds が有効な全アイテムを積む。
			for (const RenderItem& item : submittedItems)
			{
				if (item.bounds.IsValid())
				{
					debugDraw.AddWireBox(item.bounds, DEBUG_DRAW_BOUNDS_COLOR);
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
			// 10. エディタパスの宣言(FANG_ENABLE_EDITOR 内・Main 記録)
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
			// 11. Compile と Execute
			// 　宣言したパスから Compile でバリアとクリアの手順を導き、Execute でコマンドリストへ記録する。
			//------------------------------------------------------------------------
			graph.Compile();
			graph.Execute(device, jobSystem);

			//------------------------------------------------------------------------
			// 12. レンダリング統計のスナップショット更新
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
			// 13. コマンドリストを借りられなかったときの畳み
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
			// 14. EndFrame
			// 　積んだコマンドリストを渡して実行・Present・GPU の完了待ちをまとめて行う。
			//------------------------------------------------------------------------
			device.EndFrame(graph.GetCommandLists());
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
		// 5. レンダラ一式と狼モデル・ステージ
		// 　Unlit(頂点色の三角形)、MeshRenderer(狼・床・ステージ)、RenderGraph(パスとバリアの管理)、
		// 　SceneRenderer(View とカリング)を順に用意する。
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

		// 地形は失敗を FANG_FATAL にしない。読めなければ地形なしで起動が続き、狼・エディタは今までどおり。
		// ステージの接地に地表の高さが要るので、ステージより先に読む。
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

		// メッシュ側は失敗しても FANG_FATAL にしない。モデルが出ないだけならゲームは続けられるし、
		// 起動できないほうが困るため。三角形とエディタは今までどおり動く。
		MeshRenderer meshRenderer;
		WolfModel    wolf;

		// 動く 2 席（狼 2 体）は読み込みの成否によらず存在させる ➡ RenderFrame が毎フレーム
		// renderItems[0]〜[1] へ添字で書ける（詰め直しの分岐が要らない）。
		StageResources stage;
		stage.renderItems.resize(DYNAMIC_RENDER_ITEM_COUNT);

		if (meshRenderer.Initialize(device))
		{
			LoadWolf(device, meshRenderer, &wolf);

			// 地形を読めていないときだけ nullptr。ステージは接地せず glTF のまま y = 0 に置かれる。
			LoadStage(device, meshRenderer, terrain.isLoaded ? &terrain.heightmap : nullptr, &stage);
		}
		else
		{
			FANG_LOG_ERROR(Runtime, "メッシュ描画の準備に失敗した。モデルの表示だけを飛ばす");
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
		loopContext.meshRenderer    = &meshRenderer;
		loopContext.terrainRenderer = &terrainRenderer;
		loopContext.wolf            = &wolf;

		// 地形を読めていないときだけ nullptr。狼は接地せず y = 0 に立ち、同じく y = 0 のステージと足並みが
		// そろう。理由はここで 1 行だけ出す（毎フレームの経路にログを置かない）。
		loopContext.terrain = terrain.isLoaded ? &terrain.heightmap : nullptr;
		if (loopContext.terrain == nullptr)
		{
			FANG_LOG_WARNING(Runtime, "地形が無いので狼を接地しない。狼は y = 0 のまま");
		}

		// stage はここで用済み。renderItems / meshes / textures の実体を loopContext へ移す
		// （resize 済みの vector をコピーせずそのまま使い回す）。
		loopContext.renderItems   = std::move(stage.renderItems);
		loopContext.stageMeshes   = std::move(stage.meshes);
		loopContext.stageTextures = std::move(stage.textures);

		FramePipeline framePipeline;
		if (!framePipeline.Initialize(jobSystem, frameMemory, &loopContext, &UpdateFrame, &RenderFrame))
		{
			FANG_FATAL("フレームパイプラインを組めなかった");
		}

		//------------------------------------------------------------------------
		// 7. 上の層(ゲーム / エディタ)の初期化
		// 　エンジン側の参照一式を EngineContext に束ねて渡す。
		//------------------------------------------------------------------------
		// 全部の初期化が終わってから束ねる。上の層はここで受けた参照を持ち続ける。
#if FANG_ENABLE_HOT_RELOAD
		const EngineContext context{ jobSystem, frameMemory, framePipeline, &device.GetShaderReloadStatus() };
#else
		const EngineContext context{ jobSystem, frameMemory, framePipeline };
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

		// TODO: Core/Platform に時間を測る口を作る。
		auto previousTime = std::chrono::steady_clock::now();

		// ウィンドウを閉じるまでループする。WM_QUIT を受け取ると PumpMessages() が false を返す。
		while (!loopContext.hasDeviceError && window.PumpMessages())
		{
			// 前フレームからの経過時間を秒で計算する。更新と描画のどちらにも同じ値を渡す。
			const auto  currentTime      = std::chrono::steady_clock::now();
			const float deltaTimeSeconds = std::chrono::duration<float>(currentTime - previousTime).count();
			previousTime                 = currentTime;

			framePipeline.RunFrame(deltaTimeSeconds);
		}

		FANG_LOG_INFO(Runtime, "フレームループを終了");

		//------------------------------------------------------------------------
		// 9. 終了処理
		// 　作った順の逆に畳む。上の層 ➡ レンダラ ➡ デバイス ➡ ウィンドウ ➡ 土台。
		//------------------------------------------------------------------------
		framePipeline.Shutdown();
		application.OnShutdown(device);
		unlitRenderer.Shutdown(device);
#if FANG_ENABLE_DEBUG_DRAW
		debugDraw.Shutdown(device);
#endif
		sceneRenderer.Shutdown(device);
		meshRenderer.Shutdown(device); // 狼とステージのメッシュはまとめてここで解放される。
		terrainRenderer.Shutdown(device);
		device.DestroyTexture(wolf.baseColor);

		// 地形のテクスチャは TerrainRenderer にとって借用なので、持ち主のここが返す。
		device.DestroyTexture(terrain.splatmap);
		for (rhi::TextureHandle& layerAlbedo : terrain.layerAlbedos)
		{
			device.DestroyTexture(layerAlbedo);
		}

		// ステージのテクスチャも同じ理由でここが返す。
		for (const rhi::TextureHandle& textureHandle : loopContext.stageTextures)
		{
			device.DestroyTexture(textureHandle);
		}

		device.Shutdown();
		window.Shutdown();

		// フレームメモリはジョブから触られるので、ワーカーを畳んでから返す。
		jobSystem.Shutdown();
		frameMemory.Shutdown();

		return 0;
	}
} // namespace fang
