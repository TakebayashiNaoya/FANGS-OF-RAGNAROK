/**
 * @file HeightmapTerrainTests.cpp
 * @brief ハイトマップ地形のテスト。高さの補間・チャンク分割・minHeight の省略・境界クランプを確かめる。
 */
#include "Resource/HeightmapTerrain.h"
#include <doctest.h>
#include <cstdint>
#include <vector>


namespace
{
	/** @brief 全画素が同じ値のハイトマップを作る。 */
	[[nodiscard]] std::vector<uint16_t> MakeFlatHeights(uint32_t pixelCountX, uint32_t pixelCountZ, uint16_t pixel)
	{
		return std::vector<uint16_t>(static_cast<size_t>(pixelCountX) * pixelCountZ, pixel);
	}


	/** @brief テストで使い回す生成条件。100cm 四方・高さスケール 100cm。 */
	[[nodiscard]] fang::HeightmapTerrainDesc MakeDesc()
	{
		return fang::HeightmapTerrainDesc{
			.totalWidth  = 100.0f,
			.totalDepth  = 100.0f,
			.heightScale = 100.0f,
		};
	}
} // namespace


TEST_CASE("平坦なハイトマップはどこを問い合わせても同じ高さになる")
{
	// 32768 / 65535 * 100 の 1 定数がそのまま出る。
	const auto heights = MakeFlatHeights(3, 3, 32768);

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 3, 3, MakeDesc()));

	const float expected = 32768.0f / 65535.0f * 100.0f;
	CHECK(terrain.GetHeightAt(0.0f, 0.0f) == doctest::Approx(expected));
	CHECK(terrain.GetHeightAt(-50.0f, -50.0f) == doctest::Approx(expected));
	CHECK(terrain.GetHeightAt(37.5f, -12.5f) == doctest::Approx(expected));
}


TEST_CASE("既知の 2x2 でバイリニアの中間値が出る")
{
	// 左の列が 0、右の列が 65535。X 方向に 0 ➡ 100cm の斜面になる。
	const std::vector<uint16_t> heights = { 0, 65535, 0, 65535 };

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 2, 2, MakeDesc()));

	CHECK(terrain.GetHeightAt(-50.0f, 0.0f) == doctest::Approx(0.0f));
	CHECK(terrain.GetHeightAt(50.0f, 0.0f) == doctest::Approx(100.0f));

	// 中央は両端のちょうど中間。Z 方向には変化が無いので、どの Z でも同じ。
	CHECK(terrain.GetHeightAt(0.0f, 0.0f) == doctest::Approx(50.0f));
	CHECK(terrain.GetHeightAt(0.0f, 30.0f) == doctest::Approx(50.0f));

	// 1/4 の位置は 25%。
	CHECK(terrain.GetHeightAt(-25.0f, 0.0f) == doctest::Approx(25.0f));
}


TEST_CASE("範囲外の問い合わせは端へクランプされる")
{
	const std::vector<uint16_t> heights = { 0, 65535, 0, 65535 };

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 2, 2, MakeDesc()));

	CHECK(terrain.GetHeightAt(-10000.0f, 0.0f) == doctest::Approx(0.0f));
	CHECK(terrain.GetHeightAt(10000.0f, 0.0f) == doctest::Approx(100.0f));
	CHECK(terrain.GetHeightAt(0.0f, -10000.0f) == doctest::Approx(50.0f));
}


TEST_CASE("TryGetHeightAt は範囲内で GetHeightAt と同じ高さを書く")
{
	const std::vector<uint16_t> heights = { 0, 65535, 0, 65535 };

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 2, 2, MakeDesc()));

	const float sampleX[3] = { -25.0f, 0.0f, 12.5f };
	for (const float worldX : sampleX)
	{
		float height = -1.0f;
		CHECK(terrain.TryGetHeightAt(worldX, 10.0f, &height));
		CHECK(height == doctest::Approx(terrain.GetHeightAt(worldX, 10.0f)));
	}

	// 端ちょうどは範囲内。地形の縁に置いた配置を弾かない。
	float edgeHeight = -1.0f;
	CHECK(terrain.TryGetHeightAt(-50.0f, -50.0f, &edgeHeight));
	CHECK(edgeHeight == doctest::Approx(0.0f));
	CHECK(terrain.TryGetHeightAt(50.0f, 50.0f, &edgeHeight));
	CHECK(edgeHeight == doctest::Approx(100.0f));
}


TEST_CASE("TryGetHeightAt は範囲外を端へ寄せずに false を返す")
{
	const std::vector<uint16_t> heights = { 0, 65535, 0, 65535 };

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 2, 2, MakeDesc()));

	// 失敗したときは出力に触らない ➡ 呼び出し側は初期値（接地なし）をそのまま使える。
	float height = -1.0f;
	CHECK_FALSE(terrain.TryGetHeightAt(50.1f, 0.0f, &height));
	CHECK_FALSE(terrain.TryGetHeightAt(-50.1f, 0.0f, &height));
	CHECK_FALSE(terrain.TryGetHeightAt(0.0f, 10000.0f, &height));
	CHECK(height == doctest::Approx(-1.0f));

	// 同じ座標を GetHeightAt に聞けば端の高さが返る。クランプする版はそのまま残っている。
	CHECK(terrain.GetHeightAt(10000.0f, 0.0f) == doctest::Approx(100.0f));
}


TEST_CASE("TryGetHeightAt は未読み込みなら false を返す")
{
	fang::HeightmapTerrain terrain;

	float height = -1.0f;
	CHECK_FALSE(terrain.TryGetHeightAt(0.0f, 0.0f, &height));
	CHECK(height == doctest::Approx(-1.0f));
}


TEST_CASE("同じ XZ への問い合わせは何度でも同じ高さになる")
{
	// 柱の Base / Shaft / Cap のように XZ が同一の配置を別々に接地させても、積み重ねが崩れない根拠。
	const std::vector<uint16_t> heights = { 0, 20000, 40000, 65535 };

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 2, 2, MakeDesc()));

	float first  = 0.0f;
	float second = 0.0f;
	CHECK(terrain.TryGetHeightAt(13.5f, -27.5f, &first));
	CHECK(terrain.TryGetHeightAt(13.5f, -27.5f, &second));
	CHECK(first == second);
}


TEST_CASE("チャンクは指定クワッド数で分割され、縁の頂点が隣と重複する")
{
	// 5x5 画素 = 4x4 クワッドをチャンク 1 辺 2 クワッドで割ると 2x2 = 4 チャンク。
	const auto heights = MakeFlatHeights(5, 5, 0);

	auto desc           = MakeDesc();
	desc.chunkQuadCount = 2;

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 5, 5, desc));

	const auto chunks = terrain.GetChunks();
	CHECK_EQ(chunks.size(), 4);
	if (chunks.size() != 4)
	{
		return;
	}

	for (const fang::TerrainChunkSource& chunk : chunks)
	{
		// 2x2 クワッド + 縁の重複で 3x3 = 9 頂点、インデックスは 4 クワッド × 6 個。
		CHECK_EQ(chunk.positions.size(), 9);
		CHECK_EQ(chunk.normals.size(), 9);
		CHECK_EQ(chunk.indices.size(), 24);
		CHECK(chunk.bounds.IsValid());
	}

	// 左上チャンクの右端の列と、右上チャンクの左端の列は同じワールド X。継ぎ目が閉じている証拠。
	CHECK(chunks[0].bounds.max.x == doctest::Approx(chunks[1].bounds.min.x));
	CHECK(chunks[0].bounds.max.z == doctest::Approx(chunks[2].bounds.min.z));
}


TEST_CASE("割り切れない端のチャンクだけ小さくなる")
{
	// 6x6 画素 = 5x5 クワッドを 1 辺 2 クワッドで割ると 3x3 チャンクで、端の列は 1 クワッド。
	const auto heights = MakeFlatHeights(6, 6, 0);

	auto desc           = MakeDesc();
	desc.chunkQuadCount = 2;

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 6, 6, desc));

	const auto chunks = terrain.GetChunks();
	CHECK_EQ(chunks.size(), 9);
	if (chunks.size() != 9)
	{
		return;
	}

	// 並びは Z ➡ X の行順。右端（index 2）は 2x1 クワッド = 3x2 頂点。右下（index 8）は 1x1 = 2x2 頂点。
	CHECK_EQ(chunks[0].positions.size(), 9);
	CHECK_EQ(chunks[2].positions.size(), 6);
	CHECK_EQ(chunks[8].positions.size(), 4);
	CHECK_EQ(chunks[8].indices.size(), 6);
}


TEST_CASE("チャンクの頂点が 65,536 を超える条件は生成に失敗する")
{
	const auto heights = MakeFlatHeights(3, 3, 0);

	fang::HeightmapTerrain terrain;

	// 255 クワッド ➡ 256 * 256 = 65,536 頂点ちょうどで収まる。
	auto validDesc           = MakeDesc();
	validDesc.chunkQuadCount = 255;
	CHECK(terrain.BuildFromHeights(heights, 3, 3, validDesc));

	// 256 クワッド ➡ 257 * 257 頂点で上限を超える。
	auto invalidDesc           = MakeDesc();
	invalidDesc.chunkQuadCount = 256;
	CHECK_FALSE(terrain.BuildFromHeights(heights, 3, 3, invalidDesc));
	CHECK(terrain.GetChunks().empty());

	// 0 クワッドも作れない。
	auto zeroDesc           = MakeDesc();
	zeroDesc.chunkQuadCount = 0;
	CHECK_FALSE(terrain.BuildFromHeights(heights, 3, 3, zeroDesc));
}


TEST_CASE("minHeight を下回るクワッドはインデックスから消え、頂点は残る")
{
	// 3x2 画素 = 2 クワッド。左のクワッドは 4 頂点とも高さ 0、右は右端の列だけ 100cm。
	const std::vector<uint16_t> heights = { 0, 0, 65535, 0, 0, 65535 };

	auto desc      = MakeDesc();
	desc.minHeight = 10.0f;

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 3, 2, desc));

	const auto chunks = terrain.GetChunks();
	CHECK_EQ(chunks.size(), 1);
	if (chunks.size() != 1)
	{
		return;
	}

	// 残るのは右のクワッド 1 個ぶんの 6 インデックス。頂点は 3x2 = 6 個とも残る。
	CHECK_EQ(chunks[0].indices.size(), 6);
	CHECK_EQ(chunks[0].positions.size(), 6);
}


TEST_CASE("全クワッドが minHeight を下回るとチャンクごと消える")
{
	const auto heights = MakeFlatHeights(3, 3, 0);

	auto desc      = MakeDesc();
	desc.minHeight = 10.0f;

	fang::HeightmapTerrain terrain;
	CHECK(terrain.BuildFromHeights(heights, 3, 3, desc));
	CHECK(terrain.GetChunks().empty());
}


TEST_CASE("画素数や要素数が不正なら生成に失敗する")
{
	fang::HeightmapTerrain terrain;

	SUBCASE("1 列しか無い")
	{
		const auto heights = MakeFlatHeights(1, 3, 0);
		CHECK_FALSE(terrain.BuildFromHeights(heights, 1, 3, MakeDesc()));
	}

	SUBCASE("要素数が画素数と合っていない")
	{
		const auto heights = MakeFlatHeights(3, 2, 0);
		CHECK_FALSE(terrain.BuildFromHeights(heights, 3, 3, MakeDesc()));
	}

	SUBCASE("全長が 0")
	{
		const auto heights = MakeFlatHeights(3, 3, 0);

		auto desc       = MakeDesc();
		desc.totalWidth = 0.0f;
		CHECK_FALSE(terrain.BuildFromHeights(heights, 3, 3, desc));
	}

	CHECK(terrain.GetChunks().empty());
	CHECK(terrain.GetHeightAt(0.0f, 0.0f) == doctest::Approx(0.0f));
}


TEST_CASE("存在しないファイルは false を返して落ちない")
{
	fang::HeightmapTerrain terrain;
	CHECK_FALSE(terrain.Load("Z:\\存在しない\\Heightmap.dds", MakeDesc()));
	CHECK(terrain.GetChunks().empty());
}
