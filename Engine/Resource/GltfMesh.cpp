/**
 * @file GltfMesh.cpp
 * @brief glTF のメッシュ読み込みの実装。
 */
#include "Pch.h"
#include "Resource/GltfMesh.h"
#include "Resource/CgltfLoading.h"
#include "Resource/ResourceLog.h"


FANG_DEFINE_LOG_CATEGORY(Resource);


namespace fang
{
	namespace
	{
		/** @brief 1 頂点が影響を受ける関節の数。glTF の JOINTS_0 / WEIGHTS_0 は 4 つ組で固定。 */
		constexpr cgltf_size JOINT_INFLUENCE_COUNT = 4;

		/** @brief 関節の番号を 8 bit で持つので、これを超える番号が来たらエラーにする。 */
		constexpr cgltf_uint MAX_JOINT_INDEX = 255;

		/**
		 * @brief 逆バインド行列を読み、左手系へ直して詰める。
		 * @details glTF の行列は列優先で並んでいる ➡ 16 個をそのまま写すと、行優先ストレージ + 行ベクトル
		 *          規約の Matrix4x4 として正しく収まる。ここで転置を書き足すと逆に壊れる。
		 * @return 型が MAT4 でないか、読み出しに失敗したら false。
		 */
		[[nodiscard]] bool ReadInverseBindMatrices(const cgltf_accessor& accessor, std::vector<Matrix4x4>* outMatrices)
		{
			if (accessor.type != cgltf_type_mat4)
			{
				return false;
			}

			outMatrices->resize(accessor.count);
			for (cgltf_size index = 0; index < accessor.count; ++index)
			{
				Matrix4x4 matrix;
				if (cgltf_accessor_read_float(&accessor, index, &matrix.m[0][0], MATRIX_ELEMENT_COUNT) == 0)
				{
					return false;
				}

				(*outMatrices)[index] = ConvertToLeftHanded(matrix);
			}

			return true;
		}

		/**
		 * @brief JOINTS_0 を読んで詰める。
		 * @param jointCount 関節の本数。これ以上の番号が来たら壊れたデータなのでエラーにする。
		 * @return 型が VEC4 でない / 読み出しに失敗した / 番号が範囲外、のどれかなら false。
		 */
		[[nodiscard]] bool ReadJointIndices(
			const cgltf_accessor&      accessor,
			cgltf_size                 jointCount,
			std::vector<JointIndices>* outJointIndices
		)
		{
			if (accessor.type != cgltf_type_vec4)
			{
				return false;
			}

			outJointIndices->resize(accessor.count);
			for (cgltf_size index = 0; index < accessor.count; ++index)
			{
				cgltf_uint values[JOINT_INFLUENCE_COUNT] = {};
				if (cgltf_accessor_read_uint(&accessor, index, values, FANG_COUNT_OF(values)) == 0)
				{
					return false;
				}

				for (cgltf_size influence = 0; influence < JOINT_INFLUENCE_COUNT; ++influence)
				{
					if (values[influence] > MAX_JOINT_INDEX || values[influence] >= jointCount)
					{
						FANG_LOG_ERROR(
							Resource,
							"glTF の関節番号が範囲外: {}（関節は {} 本）",
							values[influence],
							jointCount
						);
						return false;
					}

					(*outJointIndices)[index].joints[influence] = static_cast<uint8_t>(values[influence]);
				}
			}

			return true;
		}

		/**
		 * @brief WEIGHTS_0 を読み、合計が 1 になるよう正規化して詰める。
		 * @details 正規化をここで済ませるとシェーダ側が割り算を持たずに済む。合計が 0 の頂点は
		 *          先頭の関節に丸ごと預ける ➡ 原点に取り残されるより、根元に付いて動くほうが異常に気づける。
		 * @return 型が VEC4 でないか、読み出しに失敗したら false。
		 */
		[[nodiscard]] bool ReadJointWeights(const cgltf_accessor& accessor, std::vector<Vector4>* outWeights)
		{
			if (accessor.type != cgltf_type_vec4)
			{
				return false;
			}

			outWeights->resize(accessor.count);
			for (cgltf_size index = 0; index < accessor.count; ++index)
			{
				float values[JOINT_INFLUENCE_COUNT] = {};
				if (cgltf_accessor_read_float(&accessor, index, values, FANG_COUNT_OF(values)) == 0)
				{
					return false;
				}

				const float total = values[0] + values[1] + values[2] + values[3];
				if (total > 0.0f)
				{
					const float scale    = 1.0f / total;
					(*outWeights)[index] = Vector4{
						values[0] * scale,
						values[1] * scale,
						values[2] * scale,
						values[3] * scale,
					};
				}
				else
				{
					(*outWeights)[index] = Vector4{ 1.0f, 0.0f, 0.0f, 0.0f };
				}
			}

			return true;
		}

		/**
		 * @brief 読んだメッシュを使っているノードから skin を引く。
		 * @details ノードを辿るのは、skin が複数ある glTF で「このメッシュの skin」を取り違えないため。
		 * @return 見つからなければ nullptr。スキンの無い glTF はここに来る。
		 */
		[[nodiscard]] const cgltf_skin* FindSkinForMesh(const cgltf_data& data, const cgltf_mesh& mesh)
		{
			for (cgltf_size index = 0; index < data.nodes_count; ++index)
			{
				const cgltf_node& node = data.nodes[index];
				if (node.mesh == &mesh && node.skin != nullptr)
				{
					return node.skin;
				}
			}

			return nullptr;
		}

		/** @brief glTF から取り出した骨の情報。Load が受け取ってメンバへ移す。 */
		struct SkinSource
		{
			std::vector<JointIndices> jointIndices;
			std::vector<Vector4>      jointWeights;
			std::vector<Matrix4x4>    inverseBindMatrices;
			std::vector<std::string>  jointNames;
		};

		/**
		 * @brief 骨の情報をまとめて読む。
		 * @details 骨が無い glTF は「読めた（中身は空）」で返す。静的なメッシュも同じ経路で扱いたいため。
		 * @return 骨があるのに一部が欠けている / 壊れている場合だけ false。
		 */
		[[nodiscard]] bool ReadSkin(
			const cgltf_data&      data,
			const cgltf_mesh&      mesh,
			const cgltf_primitive& primitive,
			SkinSource*            outSkin
		)
		{
			const cgltf_accessor* jointAccessor  = FindAttributeAccessor(primitive, cgltf_attribute_type_joints, 0);
			const cgltf_accessor* weightAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_weights, 0);
			const cgltf_skin*     skin           = FindSkinForMesh(data, mesh);

			if (jointAccessor == nullptr && weightAccessor == nullptr && skin == nullptr)
			{
				return true;
			}

			if (jointAccessor == nullptr || weightAccessor == nullptr || skin == nullptr)
			{
				FANG_LOG_ERROR(Resource, "glTF の JOINTS_0 / WEIGHTS_0 / skin のどれかが欠けている");
				return false;
			}

			if (skin->inverse_bind_matrices == nullptr)
			{
				FANG_LOG_ERROR(Resource, "glTF の skin に逆バインド行列が無い");
				return false;
			}

			// 5 本目以降の重みは JOINTS_1 に来る。狼は 4 本以内に収まっているので、来ても捨てて描き続ける。
			if (FindAttributeAccessor(primitive, cgltf_attribute_type_joints, 1) != nullptr)
			{
				FANG_LOG_WARNING(Resource, "1 頂点あたりの重みが 4 本を超えている。5 本目以降は捨てる");
			}

			if (!ReadInverseBindMatrices(*skin->inverse_bind_matrices, &outSkin->inverseBindMatrices))
			{
				FANG_LOG_ERROR(Resource, "glTF の逆バインド行列を読めなかった");
				return false;
			}

			if (outSkin->inverseBindMatrices.size() != skin->joints_count)
			{
				FANG_LOG_ERROR(
					Resource,
					"glTF の逆バインド行列の数が関節の数と合っていない: {} と {}",
					outSkin->inverseBindMatrices.size(),
					skin->joints_count
				);
				return false;
			}

			if (!ReadJointIndices(*jointAccessor, skin->joints_count, &outSkin->jointIndices))
			{
				FANG_LOG_ERROR(Resource, "glTF の JOINTS_0 を読めなかった");
				return false;
			}

			if (!ReadJointWeights(*weightAccessor, &outSkin->jointWeights))
			{
				FANG_LOG_ERROR(Resource, "glTF の WEIGHTS_0 を読めなかった");
				return false;
			}

			// 名前は ozz の並びと突き合わせる鍵になる。無名の関節があると対応表が作れないのでエラーにする。
			outSkin->jointNames.reserve(skin->joints_count);
			for (cgltf_size index = 0; index < skin->joints_count; ++index)
			{
				const cgltf_node* joint = skin->joints[index];
				if (joint == nullptr || joint->name == nullptr || joint->name[0] == '\0')
				{
					FANG_LOG_ERROR(Resource, "glTF の {} 番目の関節に名前が無い", index);
					return false;
				}

				outSkin->jointNames.emplace_back(joint->name);
			}

			return true;
		}
	} // namespace


	bool GltfMesh::Load(const char* filePath)
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
		if (data.meshes_count == 0)
		{
			FANG_LOG_ERROR(Resource, "glTF にメッシュが無い: {}", filePath);
			return false;
		}

		if (data.meshes_count > 1)
		{
			FANG_LOG_WARNING(Resource, "メッシュが {} 個ある。先頭だけ読む: {}", data.meshes_count, filePath);
		}

		const cgltf_mesh& mesh = data.meshes[0];
		if (mesh.primitives_count == 0)
		{
			FANG_LOG_ERROR(Resource, "glTF のメッシュにプリミティブが無い: {}", filePath);
			return false;
		}

		if (mesh.primitives_count > 1)
		{
			FANG_LOG_WARNING(Resource, "プリミティブが {} 個ある。先頭だけ読む: {}", mesh.primitives_count, filePath);
		}

		const cgltf_primitive& primitive = mesh.primitives[0];
		if (primitive.type != cgltf_primitive_type_triangles)
		{
			FANG_LOG_ERROR(Resource, "glTF が三角形リストではない: {}", filePath);
			return false;
		}

		if (primitive.indices == nullptr)
		{
			FANG_LOG_ERROR(Resource, "glTF にインデックスが無い: {}", filePath);
			return false;
		}

		const cgltf_accessor* positionAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_position, 0);
		const cgltf_accessor* normalAccessor   = FindAttributeAccessor(primitive, cgltf_attribute_type_normal, 0);
		const cgltf_accessor* texCoordAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_texcoord, 0);
		if (positionAccessor == nullptr || normalAccessor == nullptr || texCoordAccessor == nullptr)
		{
			FANG_LOG_ERROR(Resource, "glTF に POSITION / NORMAL / TEXCOORD_0 のどれかが無い: {}", filePath);
			return false;
		}

		if (!ReadVector3Attribute(*positionAccessor, &m_positions))
		{
			FANG_LOG_ERROR(Resource, "glTF の POSITION を読めなかった: {}", filePath);
			Clear();
			return false;
		}

		if (!ReadVector3Attribute(*normalAccessor, &m_normals))
		{
			FANG_LOG_ERROR(Resource, "glTF の NORMAL を読めなかった: {}", filePath);
			Clear();
			return false;
		}

		if (!ReadVector2Attribute(*texCoordAccessor, &m_texCoords))
		{
			FANG_LOG_ERROR(Resource, "glTF の TEXCOORD_0 を読めなかった: {}", filePath);
			Clear();
			return false;
		}

		// TANGENT は必須にしない。無ければ受け取る側（MeshRenderer::CreateMesh）が UV から作る。
		const cgltf_accessor* tangentAccessor = FindAttributeAccessor(primitive, cgltf_attribute_type_tangent, 0);
		if (tangentAccessor != nullptr && !ReadTangentAttribute(*tangentAccessor, &m_tangents))
		{
			FANG_LOG_ERROR(Resource, "glTF の TANGENT を読めなかった: {}", filePath);
			Clear();
			return false;
		}

		if (m_normals.size() != m_positions.size() || m_texCoords.size() != m_positions.size() ||
			(!m_tangents.empty() && m_tangents.size() != m_positions.size()))
		{
			FANG_LOG_ERROR(Resource, "glTF の属性ごとに頂点数が違う: {}", filePath);
			Clear();
			return false;
		}

		if (!ReadIndices(*primitive.indices, &m_indices))
		{
			Clear();
			return false;
		}

		SkinSource skin;
		if (!ReadSkin(data, mesh, primitive, &skin))
		{
			Clear();
			return false;
		}

		if (!skin.inverseBindMatrices.empty() &&
			(skin.jointIndices.size() != m_positions.size() || skin.jointWeights.size() != m_positions.size()))
		{
			FANG_LOG_ERROR(Resource, "glTF の関節番号か重みの数が頂点数と違う: {}", filePath);
			Clear();
			return false;
		}

		m_jointIndices        = std::move(skin.jointIndices);
		m_jointWeights        = std::move(skin.jointWeights);
		m_inverseBindMatrices = std::move(skin.inverseBindMatrices);
		m_jointNameStorage    = std::move(skin.jointNames);

		// 文字列の実体が動かなくなってから指す。先に作ると reserve の再確保で全部ぶら下がりになる。
		m_jointNames.reserve(m_jointNameStorage.size());
		for (const std::string& jointName : m_jointNameStorage)
		{
			m_jointNames.push_back(jointName.c_str());
		}

		// マテリアルが指す画像。無くてもエラーにしない ➡ 単色・凹凸なしで描けばよい。
		if (primitive.material != nullptr)
		{
			if (primitive.material->has_pbr_metallic_roughness != 0)
			{
				// cgltf は書かれていない係数を glTF の既定値（どちらも 1.0）で埋めて返すので、そのまま写せばよい。
				m_metallicFactor  = primitive.material->pbr_metallic_roughness.metallic_factor;
				m_roughnessFactor = primitive.material->pbr_metallic_roughness.roughness_factor;

				const cgltf_texture* baseColorTexture =
					primitive.material->pbr_metallic_roughness.base_color_texture.texture;
				if (baseColorTexture != nullptr && baseColorTexture->image != nullptr &&
					baseColorTexture->image->uri != nullptr)
				{
					// URI は空白などがパーセント符号化されていることがある。写しの上で戻す。
					m_baseColorImagePath = baseColorTexture->image->uri;
					m_baseColorImagePath.resize(cgltf_decode_uri(m_baseColorImagePath.data()));
				}
			}

			// 法線マップは pbrMetallicRoughness の外にある ➡ 上の分岐に入れない。
			const cgltf_texture* normalTexture = primitive.material->normal_texture.texture;
			if (normalTexture != nullptr && normalTexture->image != nullptr && normalTexture->image->uri != nullptr)
			{
				m_normalImagePath = normalTexture->image->uri;
				m_normalImagePath.resize(cgltf_decode_uri(m_normalImagePath.data()));

				m_normalScale = primitive.material->normal_texture.scale;
			}
		}

		FANG_LOG_INFO(
			Resource,
			"glTF を読んだ: 頂点 {} / 三角形 {} / 関節 {}: {}",
			m_positions.size(),
			m_indices.size() / TRIANGLE_INDEX_COUNT,
			m_jointNames.size(),
			filePath
		);

		return true;
	}


	void GltfMesh::Clear()
	{
		m_positions.clear();
		m_normals.clear();
		m_texCoords.clear();
		m_tangents.clear();
		m_indices.clear();

		// 名前を指すポインタから先に捨てる。実体が消えた後に残っているとぶら下がりになる。
		m_jointNames.clear();
		m_jointNameStorage.clear();
		m_jointIndices.clear();
		m_jointWeights.clear();
		m_inverseBindMatrices.clear();
		m_baseColorImagePath.clear();
		m_normalImagePath.clear();

		m_metallicFactor  = 1.0f;
		m_roughnessFactor = 1.0f;
		m_normalScale     = 1.0f;
	}
} // namespace fang
