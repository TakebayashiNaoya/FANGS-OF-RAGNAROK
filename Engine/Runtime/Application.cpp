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
#include "Renderer/MeshRenderer.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/UnlitRenderer.h"
#include "Resource/DdsImage.h"
#include "Resource/GltfMesh.h"
#include "Runtime/FramePipeline.h"
#include "Runtime/RuntimeLog.h"
#include <chrono>
#include <cmath>
#include <string>
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

		// 狼の頂点の実測範囲は X[-92.6, 111.4]（体長 204）、Y[-0.39, 106.6]（高さ 107）、Z[±18.2]（幅 36）。
		// 単位は 1 = 1cm。狼は X 軸に沿って立っているので、真横に当たる Z 方向から見るのが素直。
		constexpr Vector3 CAMERA_TARGET{ 9.4f, 53.0f, 0.0f };
		constexpr Vector3 CAMERA_UP{ 0.0f, 1.0f, 0.0f };

		/** @brief 垂直画角。ラジアン。 */
		constexpr float CAMERA_FIELD_OF_VIEW_Y_RADIANS = 60.0f * PI / 180.0f;

		// 注視点からカメラまでの距離。カメラは水平に一周するので、どの角度でも全身が収まる距離が要る。
		// 横: 16:9 なので tan(横半角) = (16/9) * tan(30 度) = 1.026 ➡ 横半角 45.7 度。水平面での外接円の
		// 半径は sqrt(102^2 + 18.2^2) = 103.6 なので 103.6 / sin(45.7 度) = 144.5 あればよい。
		// 縦: 手前を向いた縁でも高さは 107 あるため、外接円の外側に 53.5 / tan(30 度) = 92.7 が要る ➡ 196.2。
		// 縦のほうが厳しいので、そちらに 15% ほど余白を足した値にする。
		constexpr float CAMERA_DISTANCE = 225.0f;

		// 近平面・遠平面。狼が 100〜200 単位なので、0.1 のような近さに置くと深度の精度を捨てることになる。
		// 手前の面が奥を隠しているかを見るのが目的なので、狼の手前（225 - 117 = 108）より少し内側に置けば足りる。
		constexpr float CAMERA_NEAR_Z = 10.0f;
		constexpr float CAMERA_FAR_Z  = 2000.0f;

		/** @brief カメラが狼の周りを 1 周する秒数。 */
		constexpr float CAMERA_ORBIT_SECONDS = 20.0f;

		// カメラの既定角度（cameraOrbitRadians = 0）は Z 方向から見た側面視点で、体長 204 の X 軸が画面の
		// 横方向に映る。2 体目をこの方向へずらせば、その角度で見たときに 2 体が画面上で横並びに見える。
		// 204 よりわずかに大きくして、体同士が重ならない隙間を空ける。
		/** @brief 2 体目の狼を 1 体目からずらす X 方向のオフセット（cm）。 */
		constexpr float SECOND_WOLF_OFFSET_X = 220.0f;

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
			MeshRenderer*        meshRenderer  = nullptr;
			WolfModel*           wolf          = nullptr;

			/** @brief カメラの水平回転角（ラジアン）。入力の仕組みがまだ無いので時間で回す。 */
			float cameraOrbitRadians = 0.0f;

			/** @brief バックバッファを取れなかったフレームで立つ。ループを抜ける合図。 */
			bool hasDeviceError = false;
		};


		/**
		 * @brief glTF が指す画像パスから、実際に読む .dds の絶対パスを作る。
		 * @details 画像は texconv でオフライン変換してある ➡ glTF は .png を指したままなので、
		 *          拡張子をここで差し替える。区切りの / も \ へ直す。
		 */
		[[nodiscard]] std::string MakeWolfTexturePath(std::string_view imagePath)
		{
			std::string relativePath = "Models\\";
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
			const std::string filePath = MakeWolfTexturePath(model.GetBaseColorImagePath());

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
			IApplication*        application      = nullptr;
			rhi::GraphicsDevice* device           = nullptr;
			const Window*        window           = nullptr;
			const FrameData*     frameData        = nullptr;
			uint64_t             frameIndex       = 0;
			float                deltaTimeSeconds = 0.0f;
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
				*arguments.device,   commandList,          *arguments.window,
				arguments.frameData, arguments.frameIndex, arguments.deltaTimeSeconds,
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
			// 3. View の組み立て(時間で回るカメラ)と AddView
			// 　SceneRenderer::Reset で前フレームの View を捨ててから、カメラ位置と視射影行列を計算して View を
			// 　組み立て、AddView で登録する。入力の仕組みがまだ無いので、経過時間だけでカメラを狼の周りに回す。
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

			// カメラは Y を変えずに水平に周るので、狼を中心とした円周上のオフセットを注視点へ足すだけでよい。
			const Vector3 orbitOffset{
				std::sinf(loopContext.cameraOrbitRadians) * CAMERA_DISTANCE,
				0.0f,
				std::cosf(loopContext.cameraOrbitRadians) * CAMERA_DISTANCE,
			};
			const Vector3 eye = CAMERA_TARGET + orbitOffset;

			// 最小化すると幅も高さも 0 で来る。ゼロ除算と MakePerspectiveMatrix のアサートを避けて 1 で止める。
			const float viewportWidth  = static_cast<float>(window.GetWidth() > 0 ? window.GetWidth() : 1);
			const float viewportHeight = static_cast<float>(window.GetHeight() > 0 ? window.GetHeight() : 1);

			// 光は 1 つ前のフレームの更新が書いたもの。まだ何も書かれていなければ既定の光で描く
			// ➡ Game が光を書かなくても真っ黒にはならない。
			const DirectionalLight  defaultLight{};
			const DirectionalLight& light = frameData != nullptr ? frameData->light : defaultLight;

			const View view{
				.viewProjection = Multiply(
					MakeLookAtMatrix(eye, CAMERA_TARGET, CAMERA_UP),
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

			const ViewId sceneViewId = sceneRenderer.AddView(device, view);

			//------------------------------------------------------------------------
			// 4. 狼 2 体の RenderItem 組み立てと Submit
			// 　狼のメッシュが読めていれば、1 体目と 2 体目ぶんの RenderItem を組み立てて Submit で View に積む。
			// 　読めていなければこの区画ごと飛ばし、ほかの描画は今までどおり続ける。
			//------------------------------------------------------------------------
			// graph.Execute が戻るまで生きていること。RenderFrame のローカルなので、Execute より前で
			// 宣言してあれば足りる（Submit はこの配列を指す span を控えるだけでコピーしない）。
			RenderItem wolfItems[2];

			// 狼を描く。読めていなければメッシュの描画だけを飛ばし、ほかは今までどおり続ける。
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

				// 2 体目は 1 体目の隣（X 方向）に置く。2 体とも同じ骨行列（span を共有）で描くので、
				// 同じポーズで並んで歩いて見える。
				Matrix4x4 secondWolfWorld{};
				secondWolfWorld.m[3][0] = SECOND_WOLF_OFFSET_X;

				// world が単位行列でなくなったので、bounds は毎回 world で変換して埋める。
				wolfItems[0] = RenderItem{
					.mesh             = wolf.mesh,
					.bounds           = TransformAabb(localBounds, Matrix4x4{}),
					.skinningMatrices = wolf.skinningMatrices,
					.baseColor        = wolf.baseColor,
					.metallicFactor   = wolf.metallicFactor,
					.roughnessFactor  = wolf.roughnessFactor,
				};
				wolfItems[1] = RenderItem{
					.mesh             = wolf.mesh,
					.world            = secondWolfWorld,
					.bounds           = TransformAabb(localBounds, secondWolfWorld),
					.skinningMatrices = wolf.skinningMatrices,
					.baseColor        = wolf.baseColor,
					.metallicFactor   = wolf.metallicFactor,
					.roughnessFactor  = wolf.roughnessFactor,
				};

				sceneRenderer.Submit(sceneViewId, wolfItems);
			}

			//------------------------------------------------------------------------
			// 5. Unlit パスの宣言(クリアを持つ・三角形が先の理由は既存コメントにある)
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
			// 6. ScenePass の宣言(Load)
			// 　狼を描く ScenePass を View ごとに宣言する。
			//------------------------------------------------------------------------
			// 狼（1 体〜2 体）を描く ScenePass を View ごとに宣言する。三角形パスが画面をクリア済みなので、
			// 最初の View も Load（前のパスが描いた画の上に重ねる）。
			sceneRenderer.AddPasses(
				graph,
				backBufferResource,
				depthBufferResource,
				shadowMapResource,
				BACKGROUND_COLOR,
				EnLoadOperation::Load
			);

#if FANG_ENABLE_EDITOR
			//------------------------------------------------------------------------
			// 7. エディタパスの宣言(FANG_ENABLE_EDITOR 内・Main 記録)
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
			// 8. Compile と Execute
			// 　宣言したパスから Compile でバリアとクリアの手順を導き、Execute でコマンドリストへ記録する。
			//------------------------------------------------------------------------
			graph.Compile();
			graph.Execute(device, jobSystem);

			//------------------------------------------------------------------------
			// 9. コマンドリストを借りられなかったときの畳み
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
			// 10. EndFrame
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
		// 5. レンダラ一式と狼モデル
		// 　Unlit(頂点色の三角形)、MeshRenderer(狼)、RenderGraph(パスとバリアの管理)、
		// 　SceneRenderer(View とカリング)を順に用意する。
		//------------------------------------------------------------------------
		UnlitRenderer unlitRenderer;
		if (!unlitRenderer.Initialize(device))
		{
			FANG_FATAL("頂点色描画の準備に失敗した");
		}

		// メッシュ側は失敗しても FANG_FATAL にしない。モデルが出ないだけならゲームは続けられるし、
		// 起動できないほうが困るため。三角形とエディタは今までどおり動く。
		MeshRenderer meshRenderer;
		WolfModel    wolf;
		if (meshRenderer.Initialize(device))
		{
			LoadWolf(device, meshRenderer, &wolf);
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

		//------------------------------------------------------------------------
		// 6. フレームパイプライン
		// 　更新(ジョブ)と描画(メインスレッド)を並走させる仕組み。
		// 　描画が読むのは 1 つ前のフレームの更新が作った FrameData。
		// 　UpdateFrame / RenderFrame の関数ポインタと、そこへ渡す持ち物一式を組む。
		//------------------------------------------------------------------------
		FrameLoopContext loopContext{};
		loopContext.application   = &application;
		loopContext.device        = &device;
		loopContext.window        = &window;
		loopContext.jobSystem     = &jobSystem;
		loopContext.renderGraph   = &renderGraph;
		loopContext.sceneRenderer = &sceneRenderer;
		loopContext.unlitRenderer = &unlitRenderer;
		loopContext.meshRenderer  = &meshRenderer;
		loopContext.wolf          = &wolf;

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
		const EngineContext context{ jobSystem, frameMemory, framePipeline };
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
		sceneRenderer.Shutdown(device);
		meshRenderer.Shutdown(device);
		device.DestroyTexture(wolf.baseColor);
		device.Shutdown();
		window.Shutdown();

		// フレームメモリはジョブから触られるので、ワーカーを畳んでから返す。
		jobSystem.Shutdown();
		frameMemory.Shutdown();

		return 0;
	}
} // namespace fang
