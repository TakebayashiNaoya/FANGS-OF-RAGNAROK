/**
 * @file GltfMeshTests.cpp
 * @brief glTF 読み込みのテスト。非 ASCII を含むパスから読めることを確かめる。
 */
#include "Core/Platform/FileSystem.h"
#include "Resource/GltfMesh.h"
#include "NonAsciiTestDirectory.h"
#include <doctest.h>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>


namespace
{
	/** @brief 三角形 1 枚だけの最小の glTF。POSITION / NORMAL / TEXCOORD_0 と頂点バッファは Model.bin に置く。 */
	constexpr std::string_view MINIMUM_GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0] } ],
		"nodes": [ { "mesh": 0 } ],
		"meshes": [
			{
				"primitives": [
					{
						"attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
						"indices": 3,
						"mode": 4
					}
				]
			}
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 102 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 36, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 72, "byteLength": 24 },
			{ "buffer": 0, "byteOffset": 96, "byteLength": 6 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
			{ "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" }
		]
	})GLTF";


	/** @brief バイト列の末尾に、任意の POD 配列をそのまま足す。 */
	template <typename T, size_t N> void AppendBytes(std::vector<uint8_t>* buffer, const T (&values)[N])
	{
		const auto* bytes = reinterpret_cast<const uint8_t*>(values);
		buffer->insert(buffer->end(), bytes, bytes + sizeof(values));
	}

	/**
	 * @brief Model.bin の中身を組み立てる。
	 * @details 並びは MINIMUM_GLTF_JSON の bufferViews のオフセットと一致させてある。
	 *          POSITION(36) → NORMAL(36) → TEXCOORD_0(24) → indices(6) の順。
	 */
	[[nodiscard]] std::vector<uint8_t> MakeVertexBufferBytes()
	{
		constexpr float    positions[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		constexpr float    normals[9]   = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		constexpr float    texCoords[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		constexpr uint16_t indices[3]   = { 0, 1, 2 };

		std::vector<uint8_t> buffer;
		buffer.reserve(sizeof(positions) + sizeof(normals) + sizeof(texCoords) + sizeof(indices));
		AppendBytes(&buffer, positions);
		AppendBytes(&buffer, normals);
		AppendBytes(&buffer, texCoords);
		AppendBytes(&buffer, indices);
		return buffer;
	}

	/** @brief バイト列をファイルへ丸ごと書く。テストの下ごしらえ専用なので失敗したら止める。 */
	[[nodiscard]] bool WriteWholeFile(const std::string& utf8Path, std::span<const uint8_t> bytes)
	{
		std::FILE* file = fang::OpenFile(utf8Path.c_str(), "wb");
		if (file == nullptr)
		{
			return false;
		}

		const bool isWritten = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
		std::fclose(file);
		return isWritten;
	}

	/** @brief テキストをファイルへ丸ごと書く。 */
	[[nodiscard]] bool WriteWholeTextFile(const std::string& utf8Path, std::string_view text)
	{
		const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
		return WriteWholeFile(utf8Path, std::span<const uint8_t>(bytes, text.size()));
	}
} // namespace


TEST_CASE("非 ASCII を含むディレクトリの glTF を読める")
{
	fang::test::NonAsciiTestDirectory directory(L"モデル読み込みテスト_日本語パス");

	const std::string gltfPath = directory.MakeFilePath("Model.gltf");
	const std::string binPath  = directory.MakeFilePath("Model.bin");

	const bool isBinWritten  = WriteWholeFile(binPath, MakeVertexBufferBytes());
	const bool isGltfWritten = WriteWholeTextFile(gltfPath, MINIMUM_GLTF_JSON);
	CHECK(isBinWritten);
	CHECK(isGltfWritten);
	if (!isBinWritten || !isGltfWritten)
	{
		return;
	}

	fang::GltfMesh mesh;
	CHECK(mesh.Load(gltfPath.c_str()));
	CHECK(mesh.GetPositions().size() == 3);
	CHECK(mesh.GetNormals().size() == 3);
	CHECK(mesh.GetTexCoords().size() == 3);
	CHECK(mesh.GetIndices().size() == 3);
}
