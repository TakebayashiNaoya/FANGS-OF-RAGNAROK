/**
 * @file GltfScene.cpp
 * @brief glTF のシーングラフ読み込みの実装。
 */
#include "Pch.h"
#include "Resource/GltfScene.h"
#include "Resource/CgltfLoading.h"
#include "Resource/ResourceLog.h"


namespace fang
{
	namespace
	{
		/** @brief ノード階層をここまで辿ったら打ち切る。壊れたファイルでスタックを使い切らないための保険。 */
		constexpr int MAX_TRAVERSAL_DEPTH = 64;

		/** @brief 頂点数の上限。16bit のインデックスで指せる頂点は 0〜65535 の 65,536 個まで。 */
		constexpr cgltf_size MAX_VERTEX_COUNT = 65536;

		/** @brief メッシュ番号として使わない値。プリミティブが読み込めなかったことを表す。 */
		constexpr size_t INVALID_MESH_INDEX = static_cast<size_t>(-1);

		/** @brief 名前が無いノード / メッシュをログに出すときの代わりの文字列。 */
		constexpr const char* UNNAMED_LABEL = "(無名)";

		/** @brief nullptr かもしれない cgltf の名前を、ログに出せる文字列へ変える。 */
		[[nodiscard]] const char* ToDisplayName(const char* name)
		{
			return (name != nullptr && name[0] != '\0') ? name : UNNAMED_LABEL;
		}

		/**
		 * @brief 行列の左上 3x3 の行列式を求める。
		 * @details 負なら鏡像配置（ノード自身が負のスケールを持つ）。ConvertToLeftHanded の前後で
		 *          符号は変わらない（S M S の行列式は det(S)^2 det(M) = det(M)）ので、どちらの行列で
		 *          求めても同じ判定になる。
		 */
		[[nodiscard]] float ComputeUpperLeft3x3Determinant(const Matrix4x4& matrix)
		{
			const float m00 = matrix.m[0][0];
			const float m01 = matrix.m[0][1];
			const float m02 = matrix.m[0][2];
			const float m10 = matrix.m[1][0];
			const float m11 = matrix.m[1][1];
			const float m12 = matrix.m[1][2];
			const float m20 = matrix.m[2][0];
			const float m21 = matrix.m[2][1];
			const float m22 = matrix.m[2][2];

			return m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) + m02 * (m10 * m21 - m11 * m20);
		}
	} // namespace


	bool GltfScene::Load(const char* filePath)
	{
		Clear();

		if (filePath == nullptr || filePath[0] == '\0')
		{
			FANG_LOG_ERROR(Resource, "glTF のパスが空");
			return false;
		}

		CgltfDataHolder holder;
		if (!LoadCgltfFile(filePath, &holder))
		{
			return false;
		}

		const cgltf_data& data = *holder.GetData();

		// どのシーンにも属さないノードまで拾ってしまうので、data.nodes は舐めない。
		const cgltf_scene* scene =
			data.scene != nullptr ? data.scene : (data.scenes_count > 0 ? &data.scenes[0] : nullptr);
		if (scene == nullptr)
		{
			FANG_LOG_WARNING(Resource, "glTF にシーンが無い: {}", filePath);
		}
		else
		{
			for (cgltf_size nodeIndex = 0; nodeIndex < scene->nodes_count; ++nodeIndex)
			{
				if (scene->nodes[nodeIndex] != nullptr)
				{
					TraverseNode(*scene->nodes[nodeIndex], 1);
				}
			}
		}

		// span / string_view はここで初めて作る。読みながら作ると vector の再確保でぶら下がりになる。
		m_meshes.reserve(m_meshStorage.size());
		for (const MeshStorage& storage : m_meshStorage)
		{
			GltfSceneMesh mesh;
			mesh.positions          = storage.positions;
			mesh.normals            = storage.normals;
			mesh.texCoords          = storage.texCoords;
			mesh.indices            = storage.indices;
			mesh.baseColorImagePath = storage.baseColorImagePath;
			mesh.metallicFactor     = storage.metallicFactor;
			mesh.roughnessFactor    = storage.roughnessFactor;
			m_meshes.push_back(mesh);
		}

		m_instances.reserve(m_instanceStorage.size());
		for (const InstanceStorage& storage : m_instanceStorage)
		{
			GltfSceneInstance instance;
			instance.meshIndex = storage.meshIndex;
			instance.world     = storage.world;
			instance.name      = storage.name;
			m_instances.push_back(instance);
		}

		FANG_LOG_INFO(
			Resource,
			"glTF のシーンを読んだ: メッシュ {} 個 / 配置 {} 個 / 捨てた {} 個: {}",
			m_meshes.size(),
			m_instances.size(),
			m_discardedMeshCount,
			filePath
		);

		return true;
	}


	void GltfScene::TraverseNode(const cgltf_node& node, int depth)
	{
		if (depth > MAX_TRAVERSAL_DEPTH)
		{
			FANG_LOG_WARNING(
				Resource,
				"glTF のノード階層が {} 段を超えている。それ以上の枝は捨てる: {}",
				MAX_TRAVERSAL_DEPTH,
				ToDisplayName(node.name)
			);
			return;
		}

		if (node.mesh != nullptr)
		{
			const cgltf_mesh& mesh = *node.mesh;
			for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
			{
				const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
				const size_t           meshIndex = FindOrAddMesh(mesh, primitive, primitiveIndex);
				if (meshIndex == INVALID_MESH_INDEX)
				{
					continue;
				}

				// 座標系: 頂点は右手系から左手系への Z 反転をそのまま踏み（CgltfLoading 側）、
				// ノードのワールド行列には ConvertToLeftHanded を掛ける。2 つが合わさって正しい
				// 左手系のワールド座標になる（片方だけだと配置が Z 方向に鏡像になる）。
				Matrix4x4 world;
				cgltf_node_transform_world(&node, &world.m[0][0]);
				world = ConvertToLeftHanded(world);

				if (ComputeUpperLeft3x3Determinant(world) < 0.0f)
				{
					FANG_LOG_WARNING(
						Resource,
						"glTF のノードが鏡像配置になっている（負のスケール）: {}",
						ToDisplayName(node.name)
					);
				}

				InstanceStorage instance;
				instance.meshIndex = meshIndex;
				instance.world     = world;
				instance.name      = node.name != nullptr ? node.name : "";
				m_instanceStorage.push_back(std::move(instance));
			}
		}

		for (cgltf_size childIndex = 0; childIndex < node.children_count; ++childIndex)
		{
			if (node.children[childIndex] != nullptr)
			{
				TraverseNode(*node.children[childIndex], depth + 1);
			}
		}
	}


	size_t GltfScene::FindOrAddMesh(const cgltf_mesh& mesh, const cgltf_primitive& primitive, size_t primitiveIndex)
	{
		for (const auto& entry : m_primitiveToMeshIndex)
		{
			if (entry.first == &primitive)
			{
				return entry.second;
			}
		}

		MeshStorage storage;
		if (!ReadPrimitiveIntoStorage(primitive, mesh.name, primitiveIndex, &storage))
		{
			m_primitiveToMeshIndex.emplace_back(&primitive, INVALID_MESH_INDEX);
			++m_discardedMeshCount;
			return INVALID_MESH_INDEX;
		}

		const size_t meshIndex = m_meshStorage.size();
		m_meshStorage.push_back(std::move(storage));
		m_primitiveToMeshIndex.emplace_back(&primitive, meshIndex);
		return meshIndex;
	}


	bool GltfScene::ReadPrimitiveIntoStorage(
		const cgltf_primitive& primitive,
		const char*            meshName,
		size_t                 primitiveIndex,
		MeshStorage*           outStorage
	) const
	{
		const char* const displayName = ToDisplayName(meshName);

		if (primitive.type != cgltf_primitive_type_triangles)
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF が三角形リストではないプリミティブを含む。捨てる: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		if (primitive.indices == nullptr)
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF にインデックスが無いプリミティブを含む。捨てる: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		const cgltf_accessor* positionAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_position, 0);
		if (positionAccessor == nullptr)
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF に POSITION が無いプリミティブを含む。捨てる: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		// 実際に読む前にアクセサの count だけを見て弾く。65,537 頂点以上あると 16bit のインデックスで
		// 全頂点を指し切れない。
		if (positionAccessor->count > MAX_VERTEX_COUNT)
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF の頂点数が上限（{}）を超えている。捨てる: {} のプリミティブ {}（{} 頂点）",
				MAX_VERTEX_COUNT,
				displayName,
				primitiveIndex,
				positionAccessor->count
			);
			return false;
		}

		if (!ReadVector3Attribute(*positionAccessor, &outStorage->positions))
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF の POSITION を読めなかった: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		const cgltf_accessor* normalAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_normal, 0);
		if (normalAccessor != nullptr && !ReadVector3Attribute(*normalAccessor, &outStorage->normals))
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF の NORMAL を読めなかった: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		const cgltf_accessor* texCoordAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_texcoord, 0);
		if (texCoordAccessor != nullptr && !ReadVector2Attribute(*texCoordAccessor, &outStorage->texCoords))
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF の TEXCOORD_0 を読めなかった: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		if ((!outStorage->normals.empty() && outStorage->normals.size() != outStorage->positions.size()) ||
			(!outStorage->texCoords.empty() && outStorage->texCoords.size() != outStorage->positions.size()))
		{
			FANG_LOG_ERROR(
				Resource,
				"glTF の属性ごとに頂点数が違うプリミティブを含む。捨てる: {} のプリミティブ {}",
				displayName,
				primitiveIndex
			);
			return false;
		}

		// 16bit に収まらないインデックス値などの理由は ReadIndices が自分でログに出す。ここで重ねない。
		if (!ReadIndices(*primitive.indices, &outStorage->indices))
		{
			return false;
		}

		// マテリアルが指すベースカラー画像。無くてもエラーにしない ➡ 単色で描けばよい。
		if (primitive.material != nullptr && primitive.material->has_pbr_metallic_roughness != 0)
		{
			// cgltf は書かれていない係数を glTF の既定値（どちらも 1.0）で埋めて返すので、そのまま写せばよい。
			outStorage->metallicFactor  = primitive.material->pbr_metallic_roughness.metallic_factor;
			outStorage->roughnessFactor = primitive.material->pbr_metallic_roughness.roughness_factor;

			const cgltf_texture* baseColorTexture =
				primitive.material->pbr_metallic_roughness.base_color_texture.texture;
			if (baseColorTexture != nullptr && baseColorTexture->image != nullptr &&
				baseColorTexture->image->uri != nullptr)
			{
				// URI は空白などがパーセント符号化されていることがある。写しの上で戻す。
				outStorage->baseColorImagePath = baseColorTexture->image->uri;
				outStorage->baseColorImagePath.resize(cgltf_decode_uri(outStorage->baseColorImagePath.data()));
			}
		}

		return true;
	}


	void GltfScene::Clear()
	{
		// span / string_view を指す側から先に捨てる。実体（m_meshStorage / m_instanceStorage）を
		// 先に捨てると、捨てている最中が一瞬ぶら下がりの状態になる。
		m_meshes.clear();
		m_instances.clear();

		m_meshStorage.clear();
		m_instanceStorage.clear();
		m_primitiveToMeshIndex.clear();
		m_discardedMeshCount = 0;
	}
} // namespace fang
