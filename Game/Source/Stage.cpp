/**
 * @file Stage.cpp
 * @brief ステージの置き物の読み込みと、Scene オブジェクトとしての一括生成。
 */
#include "Stage.h"
#include "Collision/CollisionShapes.h"
#include "Core/Platform/AssetPath.h"
#include "RHI/GraphicsDevice.h"
#include "Resource/DdsImage.h"
#include "Resource/GltfScene.h"
#include "Resource/HeightmapTerrain.h"
#include "CollisionLayers.h"
#include "GameLog.h"
#include <string>
#include <utility>


namespace fang::game
{
	namespace
	{
		/** @brief ステージの glTF。アセットの根っこからの相対パス。 */
		constexpr const char* STAGE_MODEL_RELATIVE_PATH = "Models\\Stage.gltf";

		/** @brief ステージのモデルが置かれている場所。テクスチャの相対パスの組み立てに使う。 */
		constexpr const char* MODEL_FOLDER_RELATIVE_PATH = "Models\\";

		/**
		 * @brief glTF が指す画像パスから、実際に読む .dds の絶対パスを作る。
		 * @details 画像は texconv でオフライン変換してある ➡ glTF は .png を指したままなので、
		 *          拡張子をここで差し替える。区切りの / も \ へ直す。
		 */
		[[nodiscard]] std::string MakeModelTexturePath(std::string_view imagePath)
		{
			std::string relativePath(MODEL_FOLDER_RELATIVE_PATH);
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
	} // namespace


	void LoadAndCreateStageObjects(
		rhi::GraphicsDevice&    device,
		MeshRenderer&           meshRenderer,
		Scene&                  scene,
		const HeightmapTerrain* terrain,
		StageModel*             outStageModel
	)
	{
		GltfScene gltfScene;

		const std::string filePath = MakeAssetPath(STAGE_MODEL_RELATIVE_PATH);
		if (!gltfScene.Load(filePath.c_str()))
		{
			FANG_LOG_ERROR(Game, "ステージを読めなかった。舞台なしで続ける: {}", filePath);
			return;
		}

		const std::span<const GltfSceneMesh>     meshes    = gltfScene.GetMeshes();
		const std::span<const GltfSceneInstance> instances = gltfScene.GetInstances();

		//------------------------------------------------------------------------
		// 1. メッシュごとに GPU へ載せる
		// 　失敗した番号は無効なまま残す ➡ 配置の生成では mesh.IsValid() を見て、その置き物だけ
		// 　MeshRendererComponent を足さない（Transform だけのオブジェクトとして残る）。
		//------------------------------------------------------------------------
		std::vector<MeshId> gpuMeshes;
		gpuMeshes.reserve(meshes.size());
		for (const GltfSceneMesh& mesh : meshes)
		{
			const MeshSource source{
				.positions = mesh.positions,
				.normals   = mesh.normals,
				.texCoords = mesh.texCoords,
				.indices   = mesh.indices,
				.tangents  = mesh.tangents,
			};
			gpuMeshes.push_back(meshRenderer.CreateMesh(device, source));
		}

		//------------------------------------------------------------------------
		// 2. 画像パスごとにテクスチャを読む
		// 　同じパスを指すメッシュが並ぶので、パスごとに 1 回だけ読む対応表を挟む。数十件なので
		// 　線形探索で足りる（二分探索や辞書を持ち出すほどの件数ではない）。
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

			const std::string imageFilePath = MakeModelTexturePath(imagePath);

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
					outStageModel->textures.push_back(handle);
				}
			}
			else
			{
				FANG_LOG_ERROR(Game, "ステージのテクスチャを読めなかった。ダミーで描く: {}", imageFilePath);
			}

			// 失敗した画像パスも登録しておく（無効なハンドルのまま）。同じ壊れたパスを指す次のメッシュで
			// また読みに行って同じ理由のログを重ねて出す、ということを避けるため。
			pathToTexture.emplace_back(std::string(imagePath), handle);
			return handle;
		};

		//------------------------------------------------------------------------
		// 3. 配置ごとに地表へ載せ、Scene オブジェクトを 1 つずつ作る
		// 　40 個の置き方を型ごとに分けず、同じ手順のくり返しで作る。Scene の上限に当たったら
		// 　CreateObject が無効なハンドルを返す（警告はそちらが出す）ので、そこで打ち切る。
		// 　castsShadow は常に false（ステージは受け専用。光の箱はキャスタの AABB の和なので、含めると
		// 　光源から見える範囲が無駄に広がり、シャドウマップ 1 テクセルが表す実寸が粗くなって狼の影が
		// 　読めなくなる）。
		//------------------------------------------------------------------------
		size_t      createdCount  = 0;
		size_t      groundedCount = 0;
		size_t      outsideCount  = 0;
		std::string firstOutsideName;

		for (const GltfSceneInstance& instance : instances)
		{
			const GameObjectHandle handle = scene.CreateObject();
			if (!handle.IsValid())
			{
				FANG_LOG_WARNING(
					Game,
					"Scene の上限に達したので置き物の生成を打ち切った（{} / {} 個）",
					createdCount,
					instances.size()
				);
				break;
			}
			++createdCount;

			// ステージの glTF はどの組み立ても最下段の底面がローカル y = 0 にそろえてある
			// ➡ 原点 XZ の地表の高さを Y へ「足す」だけで底が地表に付く（代入すると、原点が底面でない
			// 　井戸のようなメッシュがめり込む）。回転・スケールには触らない。
			Matrix4x4 world = instance.world;
			if (terrain != nullptr)
			{
				float groundHeight = 0.0f;
				if (terrain->TryGetHeightAt(world.m[3][0], world.m[3][2], &groundHeight))
				{
					world.m[3][1] += groundHeight;
					++groundedCount;
				}
				else
				{
					// 地形の外。端の高さへ寄せると崖に貼り付いて見えるので、そのまま動かさない。
					if (outsideCount == 0)
					{
						firstOutsideName = instance.name;
					}
					++outsideCount;
				}
			}

			(void)scene.SetLocalMatrix(handle, world);

			const MeshId meshId = gpuMeshes[instance.meshIndex];
			if (!meshId.IsValid())
			{
				continue;
			}

			const GltfSceneMesh& meshData    = meshes[instance.meshIndex];
			const Aabb           localBounds = meshRenderer.GetLocalBounds(meshId);

			const MeshRendererComponent meshComponent{
				.mesh        = meshId,
				.localBounds = localBounds,
				.baseColor   = findOrLoadTexture(meshData.baseColorImagePath),
				.normalMap   = findOrLoadTexture(meshData.normalImagePath),
				.materialParams =
					MaterialParams{
						.metallicFactor  = meshData.metallicFactor,
						.roughnessFactor = meshData.roughnessFactor,
						.normalScale     = meshData.normalScale,
					},
				.castsShadow = false,
				.isVisible   = true,
			};
			(void)scene.AddMeshRendererComponent(handle, meshComponent);

			if (localBounds.IsValid())
			{
				const ColliderComponent colliderComponent{
					.shapeType   = EnShapeType::OBB,
					.localBounds = localBounds,
					.isEnabled   = true,
					.layerMask   = COLLISION_LAYER_PROP,
				};
				(void)scene.AddColliderComponent(handle, colliderComponent);
			}
		}

		FANG_LOG_INFO(
			Game,
			"ステージを読んだ: メッシュ {} 個 / 配置 {} 個 / テクスチャ {} 枚",
			gpuMeshes.size(),
			createdCount,
			outStageModel->textures.size()
		);

		if (terrain == nullptr)
		{
			FANG_LOG_WARNING(Game, "地形が無いので接地しない。ステージは y = 0 のまま");
		}
		else
		{
			FANG_LOG_INFO(Game, "接地 {} 個 / 範囲外 {} 個", groundedCount, outsideCount);

			if (outsideCount > 0)
			{
				FANG_LOG_WARNING(
					Game,
					"地形の範囲外の配置は接地しない: {} 個（最初は {}）",
					outsideCount,
					firstOutsideName
				);
			}
		}
	}
} // namespace fang::game
