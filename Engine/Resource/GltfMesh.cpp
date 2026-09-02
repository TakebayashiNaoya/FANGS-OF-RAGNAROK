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

		FANG_LOG_INFO(
			Resource,
			"glTF を読んだ: 頂点 {} / 三角形 {}: {}",
			m_positions.size(),
			m_indices.size() / TRIANGLE_INDEX_COUNT,
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
	}
} // namespace fang
