/**
 * @file GltfMesh.cpp
 * @brief glTF のメッシュ読み込みの実装。
 */
#include "Pch.h"
#include "Resource/GltfMesh.h"
#include "Resource/ResourceLog.h"

// 上流のコードは /W4 /WX を想定していないので、この TU の中だけ警告を落とす。
// ThirdParty は改変しない方針なので、抑えるのは取り込む側の責任になる。
#pragma warning(push, 0)
#include "cgltf.h"
#pragma warning(pop)


FANG_DEFINE_LOG_CATEGORY(Resource);


namespace fang
{
	namespace
	{
		/** @brief 16bit のインデックスに入る最大値。超える値が来たら黙って切り詰めずエラーにする。 */
		constexpr cgltf_size MAX_INDEX_VALUE = 0xFFFF;

		/** @brief 三角形 1 枚のインデックス数。 */
		constexpr cgltf_size TRIANGLE_INDEX_COUNT = 3;

		/** @brief 1 頂点が影響を受ける関節の数。glTF の JOINTS_0 / WEIGHTS_0 は 4 つ組で固定。 */
		constexpr cgltf_size JOINT_INFLUENCE_COUNT = 4;

		/** @brief 関節の番号を 8 bit で持つので、これを超える番号が来たらエラーにする。 */
		constexpr cgltf_uint MAX_JOINT_INDEX = 255;

		/** @brief 4x4 行列の要素数。 */
		constexpr cgltf_size MATRIX_ELEMENT_COUNT = 16;

		/**
		 * @brief cgltf_data を持ち、どの経路で抜けても解放する入れ物。
		 * @details 例外を使わないので、早期 return のたびに cgltf_free を書くとどこかで必ず忘れる。
		 */
		class CgltfDataHolder
		{
		public:
			FANG_NON_COPYABLE(CgltfDataHolder);
			FANG_NON_MOVABLE(CgltfDataHolder);

			CgltfDataHolder() = default;

			/** @brief 持っているデータを解放する。 */
			~CgltfDataHolder()
			{
				if (m_data != nullptr)
				{
					cgltf_free(m_data);
				}
			}

			/** @brief cgltf_parse_file に渡す受け取り口。 */
			[[nodiscard]] cgltf_data** GetAddressOfData() { return &m_data; }

			/** @brief 読み込んだデータ。まだ読んでいなければ nullptr。 */
			[[nodiscard]] cgltf_data* GetData() const { return m_data; }


		private:
			cgltf_data* m_data = nullptr;
		};

		/** @brief cgltf の失敗をログに出す。cgltf_result は名前を持たないので番号のまま出す。 */
		void LogCgltfFailure(const char* stepName, cgltf_result result, const char* filePath)
		{
			FANG_LOG_ERROR(Resource, "glTF の{}に失敗した（{}）: {}", stepName, static_cast<int>(result), filePath);
		}

		/**
		 * @brief 属性の並びから欲しい種類のアクセサを探す。
		 * @param setIndex TEXCOORD_0 の 0 にあたる添字。
		 * @return 見つからなければ nullptr。
		 */
		[[nodiscard]] const cgltf_accessor* FindAttributeAccessor(
			const cgltf_primitive& primitive,
			cgltf_attribute_type   attributeType,
			cgltf_int              setIndex
		)
		{
			for (cgltf_size index = 0; index < primitive.attributes_count; ++index)
			{
				const cgltf_attribute& attribute = primitive.attributes[index];
				if (attribute.type == attributeType && attribute.index == setIndex)
				{
					return attribute.data;
				}
			}

			return nullptr;
		}

		/**
		 * @brief VEC3 のアクセサを読み、右手系 Y-up から左手系 Y-up へ直して詰める。
		 * @details スケールは掛けない。1 unit = 1cm は読む側が決めた解釈で glTF 側に情報が無いため、
		 *          値をそのまま持てば辻褄が合う。
		 * @return 型が VEC3 でないか、読み出しに失敗したら false。
		 */
		[[nodiscard]] bool ReadVector3Attribute(const cgltf_accessor& accessor, std::vector<Vector3>* outValues)
		{
			if (accessor.type != cgltf_type_vec3)
			{
				return false;
			}

			outValues->resize(accessor.count);
			for (cgltf_size index = 0; index < accessor.count; ++index)
			{
				float values[3] = {};
				if (cgltf_accessor_read_float(&accessor, index, values, FANG_COUNT_OF(values)) == 0)
				{
					return false;
				}

				// 右手系から左手系へ移すので Z を反転する。位置も法線も同じ。
				// これは ReadIndices の巻き順の入れ替えとセットで、片方だけだと面が裏返る。
				(*outValues)[index] = Vector3{ values[0], values[1], -values[2] };
			}

			return true;
		}

		/**
		 * @brief VEC2 のアクセサを読んで詰める。
		 * @details UV は glTF も D3D も左上原点なので、V は反転しない。
		 * @return 型が VEC2 でないか、読み出しに失敗したら false。
		 */
		[[nodiscard]] bool ReadVector2Attribute(const cgltf_accessor& accessor, std::vector<Vector2>* outValues)
		{
			if (accessor.type != cgltf_type_vec2)
			{
				return false;
			}

			outValues->resize(accessor.count);
			for (cgltf_size index = 0; index < accessor.count; ++index)
			{
				float values[2] = {};
				if (cgltf_accessor_read_float(&accessor, index, values, FANG_COUNT_OF(values)) == 0)
				{
					return false;
				}

				(*outValues)[index] = Vector2{ values[0], values[1] };
			}

			return true;
		}

		/**
		 * @brief インデックスを読み、三角形の巻き順を入れ替えて詰める。
		 * @details Z を反転すると三角形の表裏が入れ替わるので、2 個目と 3 個目を交換して元へ戻す。
		 *          これは ReadVector3Attribute の Z 反転とセットで、片方だけだと面が裏返る。
		 * @return SCALAR でない / 3 の倍数でない / 16bit に収まらない値がある、のどれかなら false。
		 */
		[[nodiscard]] bool ReadIndices(const cgltf_accessor& accessor, std::vector<uint16_t>* outIndices)
		{
			if (accessor.type != cgltf_type_scalar)
			{
				FANG_LOG_ERROR(Resource, "glTF のインデックスが SCALAR ではない");
				return false;
			}

			if (accessor.count % TRIANGLE_INDEX_COUNT != 0)
			{
				FANG_LOG_ERROR(Resource, "glTF のインデックス数が 3 の倍数ではない: {}", accessor.count);
				return false;
			}

			outIndices->resize(accessor.count);
			for (cgltf_size start = 0; start < accessor.count; start += TRIANGLE_INDEX_COUNT)
			{
				cgltf_size corners[TRIANGLE_INDEX_COUNT] = {};
				for (cgltf_size corner = 0; corner < TRIANGLE_INDEX_COUNT; ++corner)
				{
					corners[corner] = cgltf_accessor_read_index(&accessor, start + corner);
					if (corners[corner] > MAX_INDEX_VALUE)
					{
						FANG_LOG_ERROR(Resource, "glTF のインデックスが 16bit に収まらない: {}", corners[corner]);
						return false;
					}
				}

				(*outIndices)[start]     = static_cast<uint16_t>(corners[0]);
				(*outIndices)[start + 1] = static_cast<uint16_t>(corners[2]);
				(*outIndices)[start + 2] = static_cast<uint16_t>(corners[1]);
			}

			return true;
		}

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

		cgltf_options   options{};
		CgltfDataHolder holder;

		const cgltf_result parseResult = cgltf_parse_file(&options, filePath, holder.GetAddressOfData());
		if (parseResult != cgltf_result_success)
		{
			LogCgltfFailure("解析", parseResult, filePath);
			return false;
		}

		// 頂点の実体は隣の .bin にある。これを呼ばないとアクセサの読み出しが全部失敗する。
		const cgltf_result bufferResult = cgltf_load_buffers(&options, holder.GetData(), filePath);
		if (bufferResult != cgltf_result_success)
		{
			LogCgltfFailure("バッファの読み込み", bufferResult, filePath);
			return false;
		}

		const cgltf_result validateResult = cgltf_validate(holder.GetData());
		if (validateResult != cgltf_result_success)
		{
			LogCgltfFailure("検証", validateResult, filePath);
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

		if (m_normals.size() != m_positions.size() || m_texCoords.size() != m_positions.size())
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

		// マテリアルが指すベースカラー画像。無くてもエラーにしない ➡ 単色で描けばよい。
		if (primitive.material != nullptr && primitive.material->has_pbr_metallic_roughness != 0)
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
		m_indices.clear();

		// 名前を指すポインタから先に捨てる。実体が消えた後に残っているとぶら下がりになる。
		m_jointNames.clear();
		m_jointNameStorage.clear();
		m_jointIndices.clear();
		m_jointWeights.clear();
		m_inverseBindMatrices.clear();
		m_baseColorImagePath.clear();

		m_metallicFactor  = 1.0f;
		m_roughnessFactor = 1.0f;
	}
} // namespace fang
