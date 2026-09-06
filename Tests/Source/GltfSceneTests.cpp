/**
 * @file GltfSceneTests.cpp
 * @brief glTF のシーン読み込みのテスト。配置行列・メッシュの共有・失敗の飛ばしを確かめる。
 */
#include "Core/Math/Aabb.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Core/Platform/FileSystem.h"
#include "Resource/GltfScene.h"
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
	/** @brief バイト列の末尾に、任意の POD 配列をそのまま足す。 */
	template <typename T, size_t N> void AppendBytes(std::vector<uint8_t>* buffer, const T (&values)[N])
	{
		const auto* bytes = reinterpret_cast<const uint8_t*>(values);
		buffer->insert(buffer->end(), bytes, bytes + sizeof(values));
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

	/**
	 * @brief 三角形 1 枚ぶんの POSITION（36 バイト）とインデックス（6 バイト）。
	 * @details 各テストの glTF の bufferViews はこの並び（POSITION → indices）に合わせてある。
	 */
	[[nodiscard]] std::vector<uint8_t> MakeTriangleBufferBytes()
	{
		constexpr float    positions[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		constexpr uint16_t indices[3]   = { 0, 1, 2 };

		std::vector<uint8_t> buffer;
		buffer.reserve(sizeof(positions) + sizeof(indices));
		AppendBytes(&buffer, positions);
		AppendBytes(&buffer, indices);
		return buffer;
	}

	/** @brief 三角形 1 枚ぶんの glTF 一式を temp ディレクトリへ書き、読み込んで返す。 */
	[[nodiscard]] bool WriteAndLoad(
		fang::test::NonAsciiTestDirectory* directory,
		std::string_view                   json,
		std::span<const uint8_t>           binBytes,
		fang::GltfScene*                   outScene
	)
	{
		const std::string gltfPath = directory->MakeFilePath("Model.gltf");
		const std::string binPath  = directory->MakeFilePath("Model.bin");

		const bool isBinWritten  = WriteWholeFile(binPath, binBytes);
		const bool isGltfWritten = WriteWholeTextFile(gltfPath, json);
		CHECK(isBinWritten);
		CHECK(isGltfWritten);
		if (!isBinWritten || !isGltfWritten)
		{
			return false;
		}

		return outScene->Load(gltfPath.c_str());
	}

	/** @brief 2 つの箱の 6 成分が一致するか確かめる。 */
	void CheckAabbsAreEqual(const fang::Aabb& actual, const fang::Aabb& expected)
	{
		CHECK(actual.min.x == doctest::Approx(expected.min.x));
		CHECK(actual.min.y == doctest::Approx(expected.min.y));
		CHECK(actual.min.z == doctest::Approx(expected.min.z));

		CHECK(actual.max.x == doctest::Approx(expected.max.x));
		CHECK(actual.max.y == doctest::Approx(expected.max.y));
		CHECK(actual.max.z == doctest::Approx(expected.max.z));
	}
} // namespace


TEST_CASE("階層付きノードの配置行列が左手系で一致する")
{
	// 親が (10, 0, 0)、子が (0, 0, 5) 平行移動 ➡ 右手系での子のワールド平行移動は (10, 0, 5)。
	// 頂点の Z 反転とセットで効く ConvertToLeftHanded は m[3][2] の符号だけ返すので、
	// 左手系では (10, 0, -5) になる。
	constexpr std::string_view GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0] } ],
		"nodes": [
			{ "children": [1], "translation": [10.0, 0.0, 0.0], "name": "Parent" },
			{ "mesh": 0, "translation": [0.0, 0.0, 5.0], "name": "Child" }
		],
		"meshes": [
			{ "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] }
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 42 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 36, "byteLength": 6 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
		]
	})GLTF";

	fang::test::NonAsciiTestDirectory directory(L"シーン読み込みテスト_階層");
	fang::GltfScene                   scene;
	CHECK(WriteAndLoad(&directory, GLTF_JSON, MakeTriangleBufferBytes(), &scene));

	CHECK(scene.GetMeshes().size() == 1);
	CHECK(scene.GetInstances().size() == 1);
	if (scene.GetInstances().empty())
	{
		return;
	}

	const fang::Matrix4x4& world = scene.GetInstances()[0].world;
	CHECK(world.m[3][0] == doctest::Approx(10.0f));
	CHECK(world.m[3][1] == doctest::Approx(0.0f));
	CHECK(world.m[3][2] == doctest::Approx(-5.0f));
}


TEST_CASE("同じプリミティブを指す 2 ノードでメッシュ 1 個 + 配置 2 個")
{
	constexpr std::string_view GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0, 1] } ],
		"nodes": [
			{ "mesh": 0, "name": "A" },
			{ "mesh": 0, "name": "B" }
		],
		"meshes": [
			{ "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] }
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 42 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 36, "byteLength": 6 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
		]
	})GLTF";

	fang::test::NonAsciiTestDirectory directory(L"シーン読み込みテスト_共有");
	fang::GltfScene                   scene;
	CHECK(WriteAndLoad(&directory, GLTF_JSON, MakeTriangleBufferBytes(), &scene));

	CHECK(scene.GetMeshes().size() == 1);
	CHECK(scene.GetInstances().size() == 2);
	if (scene.GetInstances().size() != 2)
	{
		return;
	}

	CHECK(scene.GetInstances()[0].meshIndex == 0);
	CHECK(scene.GetInstances()[1].meshIndex == 0);
	CHECK(scene.GetInstances()[0].name == "A");
	CHECK(scene.GetInstances()[1].name == "B");
}


TEST_CASE("ワールド AABB が配置ぶんずれる")
{
	constexpr std::string_view GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0] } ],
		"nodes": [
			{ "mesh": 0, "translation": [5.0, 0.0, 0.0], "name": "Only" }
		],
		"meshes": [
			{ "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] }
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 42 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 36, "byteLength": 6 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
		]
	})GLTF";

	fang::test::NonAsciiTestDirectory directory(L"シーン読み込みテスト_AABB");
	fang::GltfScene                   scene;
	CHECK(WriteAndLoad(&directory, GLTF_JSON, MakeTriangleBufferBytes(), &scene));

	CHECK(scene.GetMeshes().size() == 1);
	CHECK(scene.GetInstances().size() == 1);
	if (scene.GetMeshes().empty() || scene.GetInstances().empty())
	{
		return;
	}

	// メッシュ空間の三角形は (0,0,0) (1,0,0) (0,1,0)。Z 成分が無いので反転しても形は変わらない。
	const fang::Aabb localBounds = fang::MakeAabbFromPoints(scene.GetMeshes()[0].positions);
	const fang::Aabb worldBounds = fang::TransformAabb(localBounds, scene.GetInstances()[0].world);

	CheckAabbsAreEqual(worldBounds, fang::Aabb{ .min = { 5.0f, 0.0f, 0.0f }, .max = { 6.0f, 1.0f, 0.0f } });
}


TEST_CASE("三角形リスト以外のプリミティブは飛ばして残りを返す")
{
	// mesh 0 は三角形（mode 4）、mesh 1 は点群（mode 0）。POSITION アクセサ 0 番は両方から指す。
	constexpr std::string_view GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0, 1] } ],
		"nodes": [
			{ "mesh": 0, "name": "Good" },
			{ "mesh": 1, "name": "Bad" }
		],
		"meshes": [
			{ "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] },
			{ "primitives": [ { "attributes": { "POSITION": 0 }, "mode": 0 } ] }
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 42 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 36, "byteLength": 6 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
		]
	})GLTF";

	fang::test::NonAsciiTestDirectory directory(L"シーン読み込みテスト_未対応プリミティブ");
	fang::GltfScene                   scene;
	CHECK(WriteAndLoad(&directory, GLTF_JSON, MakeTriangleBufferBytes(), &scene));

	CHECK(scene.GetMeshes().size() == 1);
	CHECK(scene.GetInstances().size() == 1);
	if (scene.GetInstances().empty())
	{
		return;
	}

	CHECK(scene.GetInstances()[0].name == "Good");
}


TEST_CASE("メッシュの名前(meshes[].name)を持つ。無ければ空")
{
	// ノードの name とメッシュの name は別物。読むのはメッシュ側。
	constexpr std::string_view GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0, 1] } ],
		"nodes": [
			{ "mesh": 0, "name": "NamedNode" },
			{ "mesh": 1, "name": "UnnamedNode" }
		],
		"meshes": [
			{ "name": "Marker", "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] },
			{ "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] }
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 42 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 36 },
			{ "buffer": 0, "byteOffset": 36, "byteLength": 6 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" },
			{ "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
		]
	})GLTF";

	fang::test::NonAsciiTestDirectory directory(L"シーン読み込みテスト_メッシュ名");
	fang::GltfScene                   scene;
	CHECK(WriteAndLoad(&directory, GLTF_JSON, MakeTriangleBufferBytes(), &scene));

	CHECK(scene.GetMeshes().size() == 2);
	if (scene.GetMeshes().size() != 2)
	{
		return;
	}

	CHECK(scene.GetMeshes()[0].name == "Marker");
	CHECK(scene.GetMeshes()[1].name.empty());
}


TEST_CASE("65,536 頂点超のプリミティブを検出する")
{
	// POSITION アクセサの count だけを 65,537 に偽装する。読む前に count を見て捨てるので、
	// 実際の .bin には水増しした頂点データを書かない（indices の 6 バイトだけが実体）。
	// buffer / bufferView の byteLength は cgltf_validate の桁計算をつじつま合わせするための
	// 宣言値で、実ファイルの大きさとは cgltf 側で突き合わせない。
	constexpr std::string_view GLTF_JSON = R"GLTF({
		"asset": { "version": "2.0" },
		"scene": 0,
		"scenes": [ { "nodes": [0] } ],
		"nodes": [
			{ "mesh": 0, "name": "TooBig" }
		],
		"meshes": [
			{ "primitives": [ { "attributes": { "POSITION": 1 }, "indices": 0, "mode": 4 } ] }
		],
		"buffers": [ { "uri": "Model.bin", "byteLength": 786452 } ],
		"bufferViews": [
			{ "buffer": 0, "byteOffset": 0, "byteLength": 6 },
			{ "buffer": 0, "byteOffset": 8, "byteLength": 786444 }
		],
		"accessors": [
			{ "bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR" },
			{ "bufferView": 1, "componentType": 5126, "count": 65537, "type": "VEC3" }
		]
	})GLTF";

	constexpr uint16_t realIndexBytes[3] = { 0, 1, 2 };

	std::vector<uint8_t> binBytes;
	AppendBytes(&binBytes, realIndexBytes);

	fang::test::NonAsciiTestDirectory directory(L"シーン読み込みテスト_頂点数超過");
	fang::GltfScene                   scene;
	CHECK(WriteAndLoad(&directory, GLTF_JSON, binBytes, &scene));

	CHECK(scene.GetMeshes().empty());
	CHECK(scene.GetInstances().empty());
}
