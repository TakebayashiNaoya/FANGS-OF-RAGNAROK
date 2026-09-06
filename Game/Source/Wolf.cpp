/**
 * @file Wolf.cpp
 * @brief 狼のモデルの読み込みと、Scene オブジェクトとしての生成。
 */
#include "Wolf.h"
#include "Collision/CollisionShapes.h"
#include "Collision/CollisionWorld.h"
#include "Core/Platform/AssetPath.h"
#include "RHI/GraphicsDevice.h"
#include "Resource/DdsImage.h"
#include "Resource/GltfMesh.h"
#include "CollisionAttribute.h"
#include "GameLog.h"
#include "WolfBehavior.h"
#include <span>
#include <string>


namespace fang::game
{
	namespace
	{
		/** @brief 狼の glTF。アセットの根っこからの相対パス。 */
		constexpr const char* WOLF_MODEL_RELATIVE_PATH = "Models\\Wolf.gltf";

		// 骨とクリップは gltf2ozz が Wolf.gltf から出したもの。歩き（直進）はルートモーションを持たないので、
		// その場で脚だけが動く ➡ 移動処理が無くても再生の正しさが確かめられる。
		constexpr const char* WOLF_SKELETON_RELATIVE_PATH = "Models\\WolfSkeleton.ozz";
		constexpr const char* WOLF_CLIP_RELATIVE_PATH     = "Models\\A_WalkSlow_F.ozz";

		/** @brief 狼のモデルが置かれている場所。テクスチャの相対パスの組み立てに使う。 */
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

		/**
		 * @brief 狼のマテリアルが指す画像を 1 枚読んで GPU へ載せる。
		 * @param imagePath glTF からの相対パス。空なら何もせず無効なハンドルを返す。
		 * @param usageName ログに出す用途の名前（「ベースカラー」など）。
		 * @details 失敗しても落とさない。無効なハンドルのままなら、レンダラがダミーを差す。
		 */
		[[nodiscard]] rhi::TextureHandle LoadWolfTexture(
			rhi::GraphicsDevice& device,
			std::string_view     imagePath,
			const char*          usageName
		)
		{
			if (imagePath.empty())
			{
				return rhi::TextureHandle{};
			}

			// DdsImage は転送が済めば用済み。5MB の中身をこの関数を抜けるところで手放す。
			const std::string filePath = MakeModelTexturePath(imagePath);

			DdsImage image;
			if (!image.Load(filePath.c_str()))
			{
				FANG_LOG_ERROR(Game, "狼の{}を読めなかった。ダミーで描く: {}", usageName, filePath);
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
				FANG_LOG_ERROR(Game, "狼のスケルトンを読めなかった。バインドポーズで描く: {}", skeletonPath);
				return;
			}

			const std::string clipPath = MakeAssetPath(WOLF_CLIP_RELATIVE_PATH);
			if (!outWolf->animation.LoadClip(clipPath.c_str()))
			{
				FANG_LOG_ERROR(Game, "狼のクリップを読めなかった。バインドポーズで描く: {}", clipPath);
				return;
			}

			// gltf2ozz は関節を並べ替える。名前で対応表を作らないと、骨の対応がずれた姿勢が描かれる。
			if (!outWolf->animation.BuildJointRemap(model.GetJointNames()))
			{
				FANG_LOG_ERROR(Game, "狼の関節の対応表を作れなかった。バインドポーズで描く");
				return;
			}

			outWolf->playback.SetDurationSeconds(outWolf->animation.GetClipDurationSeconds());

			FANG_LOG_INFO(Game, "狼のアニメーションを読んだ: {:.3f} 秒", outWolf->playback.GetDurationSeconds());
		}
	} // namespace


	void LoadWolfModel(rhi::GraphicsDevice& device, MeshRenderer& meshRenderer, WolfModel* outWolf)
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
			FANG_LOG_ERROR(Game, "狼のモデルを読めなかった: {}", filePath);
			return;
		}

		//------------------------------------------------------------------------
		// 2. 頂点・骨・テクスチャの取り出しと GPU リソース化
		// 　ベースカラーのテクスチャとマテリアル係数を取り出したあと、骨の有無で分岐する。骨が無ければ
		// 　頂点(位置・法線・UV)だけを取り出して CreateMesh で静的メッシュとして GPU へ載せる。骨が
		// 　あれば関節番号・重みも合わせて取り出してスキンメッシュとして載せ、続けて逆バインド行列を
		// 　写し、LoadWolfAnimation でスケルトンとクリップを読む。
		//------------------------------------------------------------------------
		outWolf->baseColor = LoadWolfTexture(device, model.GetBaseColorImagePath(), "ベースカラー");
		outWolf->normalMap = LoadWolfTexture(device, model.GetNormalImagePath(), "法線マップ");

		outWolf->metallicFactor  = model.GetMetallicFactor();
		outWolf->roughnessFactor = model.GetRoughnessFactor();
		outWolf->normalScale     = model.GetNormalScale();

		if (!model.HasSkin())
		{
			// 骨を持たない glTF なら静的メッシュとして描く。失敗の理由は CreateMesh 側がログに出す。
			const MeshSource source{
				.positions = model.GetPositions(),
				.normals   = model.GetNormals(),
				.texCoords = model.GetTexCoords(),
				.indices   = model.GetIndices(),
				.tangents  = model.GetTangents(),
			};

			outWolf->mesh = meshRenderer.CreateMesh(device, source);
			if (outWolf->mesh.IsValid())
			{
				outWolf->localBounds = meshRenderer.GetLocalBounds(outWolf->mesh);
			}
			return;
		}

		const SkinnedMeshSource source{
			.positions    = model.GetPositions(),
			.normals      = model.GetNormals(),
			.texCoords    = model.GetTexCoords(),
			.indices      = model.GetIndices(),
			.jointIndices = model.GetJointIndices(),
			.jointWeights = model.GetJointWeights(),
			.tangents     = model.GetTangents(),
		};

		outWolf->mesh = meshRenderer.CreateMesh(device, source);
		if (!outWolf->mesh.IsValid())
		{
			return;
		}

		outWolf->isSkinned   = true;
		outWolf->localBounds = meshRenderer.GetLocalBounds(outWolf->mesh);

		const std::span<const Matrix4x4> inverseBindMatrices = model.GetInverseBindMatrices();
		outWolf->inverseBindMatrices.assign(inverseBindMatrices.begin(), inverseBindMatrices.end());

		// 単位行列のまま置いておく ➡ クリップを読めなくてもバインドポーズが出る。
		outWolf->skinningMatrices.resize(inverseBindMatrices.size());

		LoadWolfAnimation(model, outWolf);
	}


	GameObjectHandle CreateWolfObject(
		Scene&                    scene,
		WolfModel&                model,
		const WolfMovementParams& params,
		const MeleeSwingParams&   swingParams,
		const HealthComponent&    healthComponent,
		CollisionWorld*           collisionWorld,
		const HeightmapTerrain*   terrain,
		const Vector3&            initialPosition,
		float                     initialFacingRadians,
		WolfBehavior**            outBehavior
	)
	{
		const GameObjectHandle handle = scene.CreateObject();
		if (!handle.IsValid())
		{
			FANG_LOG_ERROR(Game, "狼のオブジェクトを作れなかった（Scene の上限）");
			return handle;
		}

		if (model.mesh.IsValid())
		{
			const MeshRendererComponent meshRendererComponent{
				.mesh        = model.mesh,
				.localBounds = model.localBounds,
				.baseColor   = model.baseColor,
				.normalMap   = model.normalMap,
				.materialParams =
					MaterialParams{
						.metallicFactor  = model.metallicFactor,
						.roughnessFactor = model.roughnessFactor,
						.normalScale     = model.normalScale,
					},
				.castsShadow = true,
				.isVisible   = true,
			};
			(void)scene.AddMeshRendererComponent(handle, meshRendererComponent);

			// 狼は四つ足なので、体を包むカプセルのほうが箱より当たりが素直。WOLF を足して雑魚の攻撃の掃引に出す。
			const ColliderComponent colliderComponent{
				.shapeType     = EnShapeType::Capsule,
				.localBounds   = model.localBounds,
				.isEnabled     = true,
				.attributeMask = COLLISION_ATTRIBUTE_CHARACTER | COLLISION_ATTRIBUTE_WOLF,
			};
			(void)scene.AddColliderComponent(handle, colliderComponent);
		}

		(void)scene.AddHealthComponent(handle, healthComponent);

		const WolfBehavior::Dependencies dependencies{
			.collisionWorld      = collisionWorld,
			.terrain             = terrain,
			.isSkinned           = model.isSkinned,
			.animation           = model.isSkinned ? &model.animation : nullptr,
			.playback            = model.isSkinned ? &model.playback : nullptr,
			.inverseBindMatrices = model.inverseBindMatrices,
			.skinningMatricesStorage =
				model.isSkinned ? std::span<Matrix4x4>(model.skinningMatrices) : std::span<Matrix4x4>{},
		};

		WolfBehavior* behavior = scene.AddBehavior<WolfBehavior>(
			handle,
			params,
			swingParams,
			dependencies,
			initialPosition,
			initialFacingRadians
		);
		if (behavior == nullptr)
		{
			FANG_LOG_ERROR(Game, "狼の振る舞いを作れなかった（Scene の振る舞い上限）");
		}

		if (outBehavior != nullptr)
		{
			*outBehavior = behavior;
		}

		return handle;
	}
} // namespace fang::game
