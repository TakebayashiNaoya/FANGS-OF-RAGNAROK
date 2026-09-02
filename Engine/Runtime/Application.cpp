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
#include "Core/Math/Matrix4x4.h"
#include "Core/Memory/FrameAllocator.h"
#include "Core/Platform/AssetPath.h"
#include "Core/Platform/Window.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/SkinnedMeshRenderer.h"
#include "Renderer/TriangleRenderer.h"
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

		/** @brief 円周率。Core/Math がまだ定数を持っていないのでここに置く。 */
		constexpr float PI = 3.14159265f;

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
			IApplication*        application         = nullptr;
			rhi::GraphicsDevice* device              = nullptr;
			Window*              window              = nullptr;
			TriangleRenderer*    triangleRenderer    = nullptr;
			MeshRenderer*        meshRenderer        = nullptr;
			SkinnedMeshRenderer* skinnedMeshRenderer = nullptr;
			WolfModel*           wolf                = nullptr;

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
		void LoadWolf(
			rhi::GraphicsDevice& device,
			MeshRenderer&        meshRenderer,
			SkinnedMeshRenderer& skinnedMeshRenderer,
			WolfModel*           outWolf
		)
		{
			// GltfMesh は CreateMesh が済めば用済み。15MB の .bin 由来の配列を抱え続けないよう、
			// この関数を抜けるところで手放す。逆バインド行列と関節名だけは写しを残す。
			GltfMesh model;

			const std::string filePath = MakeAssetPath(WOLF_MODEL_RELATIVE_PATH);
			if (!model.Load(filePath.c_str()))
			{
				FANG_LOG_ERROR(Runtime, "狼のモデルを読めなかった: {}", filePath);
				return;
			}

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

			outWolf->mesh = skinnedMeshRenderer.CreateMesh(device, source);
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

		/** @brief 描画の本体。RHI を触るのはここだけなので、メインスレッドの持ち物が全部そろっている。 */
		void RenderFrame(void* userData, const FrameData* frameData, uint64_t frameIndex, float deltaTimeSeconds)
		{
			auto& loopContext = *static_cast<FrameLoopContext*>(userData);

			rhi::GraphicsDevice& device = *loopContext.device;
			Window&              window = *loopContext.window;

			// リサイズは更新と関わらないので、ジョブを投げた後のここで済ませる。BeginFrame の中では作り直せない。
			if (window.ConsumeSizeChange())
			{
				device.Resize(window.GetWidth(), window.GetHeight());
			}

			// このフレームの記録準備（記録メモリの巻き戻し、バックバッファの描き込み先への切り替え、クリア）を
			// 頼み、描画コマンドの書き込み先を受け取る。EndFrame まで有効。
			rhi::CommandList* commandList = device.BeginFrame(BACKGROUND_COLOR);
			if (commandList == nullptr)
			{
				// ここに来るのは主にデバイスロスト。黙って畳むと「起動してすぐ閉じた」ようにしか見えないので残す。
				FANG_LOG_ERROR(Runtime, "フレーム {} でバックバッファを開けなかった。フレームループを畳む", frameIndex);
				loopContext.hasDeviceError = true;
				return;
			}

			// 三角形を描く。描画コマンドを積むだけで、まだ GPU は動かない。
			loopContext.triangleRenderer->Draw(*commandList, window.GetWidth(), window.GetHeight());

			// 狼を描く。読めていなければメッシュの描画だけを飛ばし、ほかは今までどおり続ける。
			WolfModel& wolf = *loopContext.wolf;
			if (wolf.mesh.IsValid())
			{
				// メッシュのレンダラはビューポートを設定しない。TriangleRenderer::Draw が内部で設定しているのに
				// 頼ると、描く順を入れ替えた途端に壊れる。
				commandList->SetViewport(window.GetWidth(), window.GetHeight());

				// 入力の仕組みがまだ無いので、時間でカメラを回して全方向から形と前後関係を確かめられるようにする。
				loopContext.cameraOrbitRadians += deltaTimeSeconds * (2.0f * PI / CAMERA_ORBIT_SECONDS);
				if (loopContext.cameraOrbitRadians >= 2.0f * PI)
				{
					// 積みっぱなしにすると値が大きくなるほど角度の刻みが粗くなるので、1 周ごとに戻す。
					loopContext.cameraOrbitRadians -= 2.0f * PI;
				}

				const Vector3 eye{
					CAMERA_TARGET.x + std::sinf(loopContext.cameraOrbitRadians) * CAMERA_DISTANCE,
					CAMERA_TARGET.y,
					CAMERA_TARGET.z + std::cosf(loopContext.cameraOrbitRadians) * CAMERA_DISTANCE,
				};

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

				// 実行中のヒープ確保は 0 が要件なので、std::vector を作らずスタックの配列を span で渡す。
				// world は既定の単位行列のまま。狼はモデル座標のまま原点に置く。
				if (wolf.isSkinned)
				{
					// 再生位置を進めるのはここ。カメラの回転と同じ場所に置いてある
					// ➡ Scene ができたらカメラごとゲーム側の更新へ移る。
					UpdateWolfPose(&wolf, deltaTimeSeconds);

					const SkinnedRenderItem items[] = {
						SkinnedRenderItem{
							.mesh             = wolf.mesh,
							.skinningMatrices = wolf.skinningMatrices,
							.baseColor        = wolf.baseColor,
							.metallicFactor   = wolf.metallicFactor,
							.roughnessFactor  = wolf.roughnessFactor,
						},
					};
					loopContext.skinnedMeshRenderer->Draw(device, *commandList, view, items);
				}
				else
				{
					const RenderItem items[] = {
						RenderItem{
							.mesh            = wolf.mesh,
							.baseColor       = wolf.baseColor,
							.metallicFactor  = wolf.metallicFactor,
							.roughnessFactor = wolf.roughnessFactor,
						},
					};
					loopContext.meshRenderer->Draw(*commandList, view, items);
				}
			}

			// 上の層に描画コマンドを積ませる。読ませるのは 1 つ前のフレームの更新が作ったもの。
			const FrameRenderContext context{ device, *commandList, window, frameData, frameIndex, deltaTimeSeconds };
			loopContext.application->OnRender(context);

			device.EndFrame();
		}
	} // namespace


	int RunApplication(IApplication& application)
	{
		// ジョブシステムはここが持ち、使う側へ参照で渡す。Engine ができたらそこへぶら下げ直す。
		JobSystem jobSystem;
		if (!jobSystem.Initialize(JobSystemDesc{}))
		{
			FANG_FATAL("ジョブシステムを開始できなかった");
		}

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

		Window window;
		if (!window.Initialize(WindowDesc{}))
		{
			FANG_FATAL("ウィンドウを作れなかった");
		}

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

		TriangleRenderer triangleRenderer;
		if (!triangleRenderer.Initialize(device))
		{
			FANG_FATAL("三角形の準備に失敗した");
		}

		// メッシュ側は失敗しても FANG_FATAL にしない。モデルが出ないだけならゲームは続けられるし、
		// 起動できないほうが困るため。三角形とエディタは今までどおり動く。
		MeshRenderer        meshRenderer;
		SkinnedMeshRenderer skinnedMeshRenderer;
		WolfModel           wolf;
		if (meshRenderer.Initialize(device) && skinnedMeshRenderer.Initialize(device))
		{
			LoadWolf(device, meshRenderer, skinnedMeshRenderer, &wolf);
		}
		else
		{
			FANG_LOG_ERROR(Runtime, "メッシュ描画の準備に失敗した。モデルの表示だけを飛ばす");
		}

		FrameLoopContext loopContext{};
		loopContext.application         = &application;
		loopContext.device              = &device;
		loopContext.window              = &window;
		loopContext.triangleRenderer    = &triangleRenderer;
		loopContext.meshRenderer        = &meshRenderer;
		loopContext.skinnedMeshRenderer = &skinnedMeshRenderer;
		loopContext.wolf                = &wolf;

		FramePipeline framePipeline;
		if (!framePipeline.Initialize(jobSystem, frameMemory, &loopContext, &UpdateFrame, &RenderFrame))
		{
			FANG_FATAL("フレームパイプラインを組めなかった");
		}

		// 全部の初期化が終わってから束ねる。上の層はここで受けた参照を持ち続ける。
		const EngineContext context{ jobSystem, frameMemory, framePipeline };
		if (!application.OnInitialize(context, device, window))
		{
			FANG_FATAL("上の層の初期化に失敗した");
		}

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

		framePipeline.Shutdown();
		application.OnShutdown(device);
		triangleRenderer.Shutdown(device);
		meshRenderer.Shutdown(device);
		skinnedMeshRenderer.Shutdown(device);
		device.DestroyTexture(wolf.baseColor);
		device.Shutdown();
		window.Shutdown();

		// フレームメモリはジョブから触られるので、ワーカーを畳んでから返す。
		jobSystem.Shutdown();
		frameMemory.Shutdown();

		return 0;
	}
} // namespace fang
