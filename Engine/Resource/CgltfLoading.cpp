/**
 * @file CgltfLoading.cpp
 * @brief cgltf を使った読み込みの共有部分の実装。
 */
#include "Pch.h"
#include "Resource/CgltfLoading.h"
#include "Core/Platform/FileSystem.h"
#include "Resource/ResourceLog.h"
#include <cstdio>
#include <cstdlib>


namespace fang
{
	namespace
	{
		/** @brief cgltf の失敗をログに出す。cgltf_result は名前を持たないので番号のまま出す。 */
		void LogCgltfFailure(const char* stepName, cgltf_result result, const char* filePath)
		{
			FANG_LOG_ERROR(Resource, "glTF の{}に失敗した（{}）: {}", stepName, static_cast<int>(result), filePath);
		}

		/** @brief cgltf にアロケータが渡されなかったときの確保。cgltf の既定実装と同じ malloc ベース。 */
		void* AllocateForCgltf(void* userData, cgltf_size size)
		{
			FANG_UNUSED(userData);
			return std::malloc(size);
		}

		/** @brief AllocateForCgltf で確保した領域を解放する。 */
		void FreeForCgltf(void* userData, void* pointer)
		{
			FANG_UNUSED(userData);
			std::free(pointer);
		}

		/**
		 * @brief cgltf にファイルを読ませるコールバック。.gltf 本体と、隣の .bin の両方がここを通る。
		 * @details 既定の実装は fopen を narrow 文字列で呼ぶため、Windows では現在の ANSI コードページ
		 *          として解釈され、日本語などの非 ASCII を含むパスが化けて開けない。fang::OpenFile で
		 *          UTF-16 に直してから開く以外は、サイズを測って丸ごと読む既定の手順をそのまま踏む。
		 */
		cgltf_result ReadFileForCgltf(
			const cgltf_memory_options* memoryOptions,
			const cgltf_file_options*   fileOptions,
			const char*                 path,
			cgltf_size*                 size,
			void**                      data
		)
		{
			FANG_UNUSED(fileOptions);

			std::FILE* file = OpenFile(path, "rb");
			if (file == nullptr)
			{
				return cgltf_result_file_not_found;
			}

			std::fseek(file, 0, SEEK_END);
			const long fileSize = std::ftell(file);
			std::fseek(file, 0, SEEK_SET);
			if (fileSize < 0)
			{
				std::fclose(file);
				return cgltf_result_io_error;
			}

			void* (*allocate)(void*, cgltf_size) =
				memoryOptions->alloc_func != nullptr ? memoryOptions->alloc_func : &AllocateForCgltf;

			const cgltf_size byteCount = static_cast<cgltf_size>(fileSize);
			void*            fileData  = allocate(memoryOptions->user_data, byteCount);
			if (fileData == nullptr)
			{
				std::fclose(file);
				return cgltf_result_out_of_memory;
			}

			const size_t readByteCount = std::fread(fileData, 1, byteCount, file);
			std::fclose(file);

			if (readByteCount != byteCount)
			{
				void (*deallocate)(void*, void*) =
					memoryOptions->free_func != nullptr ? memoryOptions->free_func : &FreeForCgltf;
				deallocate(memoryOptions->user_data, fileData);
				return cgltf_result_io_error;
			}

			*size = byteCount;
			*data = fileData;
			return cgltf_result_success;
		}

		/** @brief ReadFileForCgltf が確保した領域を解放するコールバック。 */
		void ReleaseFileForCgltf(
			const cgltf_memory_options* memoryOptions,
			const cgltf_file_options*   fileOptions,
			void*                       data,
			cgltf_size                  size
		)
		{
			FANG_UNUSED(fileOptions);
			FANG_UNUSED(size);

			void (*deallocate)(void*, void*) =
				memoryOptions->free_func != nullptr ? memoryOptions->free_func : &FreeForCgltf;
			deallocate(memoryOptions->user_data, data);
		}
	} // namespace


	CgltfDataHolder::~CgltfDataHolder()
	{
		if (m_data != nullptr)
		{
			cgltf_free(m_data);
		}
	}


	bool LoadCgltfFile(const char* filePath, CgltfDataHolder* outHolder)
	{
		cgltf_options options{};

		// 既定の file.read / file.release は narrow の fopen を使うので、非 ASCII パスに対応した
		// ものへ差し替える。.gltf 本体と cgltf_load_buffers が読む隣の .bin の両方がここを通る。
		options.file.read    = &ReadFileForCgltf;
		options.file.release = &ReleaseFileForCgltf;

		const cgltf_result parseResult = cgltf_parse_file(&options, filePath, outHolder->GetAddressOfData());
		if (parseResult != cgltf_result_success)
		{
			LogCgltfFailure("解析", parseResult, filePath);
			return false;
		}

		// 頂点の実体は隣の .bin にある。これを呼ばないとアクセサの読み出しが全部失敗する。
		const cgltf_result bufferResult = cgltf_load_buffers(&options, outHolder->GetData(), filePath);
		if (bufferResult != cgltf_result_success)
		{
			LogCgltfFailure("バッファの読み込み", bufferResult, filePath);
			return false;
		}

		const cgltf_result validateResult = cgltf_validate(outHolder->GetData());
		if (validateResult != cgltf_result_success)
		{
			LogCgltfFailure("検証", validateResult, filePath);
			return false;
		}

		return true;
	}


	const cgltf_accessor* FindAttributeAccessor(
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


	bool ReadVector3Attribute(const cgltf_accessor& accessor, std::vector<Vector3>* outValues)
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


	bool ReadTangentAttribute(const cgltf_accessor& accessor, std::vector<Vector4>* outValues)
	{
		if (accessor.type != cgltf_type_vec4)
		{
			return false;
		}

		outValues->resize(accessor.count);
		for (cgltf_size index = 0; index < accessor.count; ++index)
		{
			float values[4] = {};
			if (cgltf_accessor_read_float(&accessor, index, values, FANG_COUNT_OF(values)) == 0)
			{
				return false;
			}

			// 位置や法線と同じく Z を反転する。ただしこれは鏡映なので従法線の向きも裏返る
			// ➡ w も一緒に反転しないと、cross(N, T) * w が ∂P/∂v とずれて陰影が裏返る。
			(*outValues)[index] = Vector4{ values[0], values[1], -values[2], -values[3] };
		}

		return true;
	}


	bool ReadVector2Attribute(const cgltf_accessor& accessor, std::vector<Vector2>* outValues)
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


	bool ReadIndices(const cgltf_accessor& accessor, std::vector<uint16_t>* outIndices)
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
} // namespace fang
