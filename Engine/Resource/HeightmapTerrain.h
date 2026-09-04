/**
 * @file HeightmapTerrain.h
 * @brief R16 ハイトマップから地形メッシュのチャンクを生成し、高さの問い合わせに答える。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Vector3.h"
#include <cfloat>
#include <cstdint>
#include <span>
#include <vector>


namespace fang
{
	/**
	 * @brief 地形の生成条件。長さの単位はすべて cm。
	 * @details 画素 (px, pz) ➡ ワールド (originX + px × cellX, 高さ, originZ + pz × cellZ) の対応で、
	 *          origin は -total / 2（地形の中心がワールド原点）。行番号が増えると +Z。
	 *          画像の上下がツール側と合わないときは、エンジンで補正せずツール側（texconv の前）で反転する。
	 */
	struct HeightmapTerrainDesc
	{
		float totalWidth  = 0.0f; /**< X 方向の全長。0 以下は生成に失敗する。 */
		float totalDepth  = 0.0f; /**< Z 方向の全長。0 以下は生成に失敗する。 */
		float heightScale = 0.0f; /**< 画素 65535 のときの高さ。 */

		/**
		 * @brief 4 頂点すべてがこれを下回るクワッドをインデックスから省く高さ。
		 * @details 水面下のように描いても見えない領域を削るためのもの。既定の -FLT_MAX は何も省かない。
		 */
		float minHeight = -FLT_MAX;

		/**
		 * @brief チャンク 1 辺のクワッド数。端のチャンクだけ小さくなる。
		 * @details (chunkQuadCount + 1)² が 65,536 を超える値は生成に失敗する（16 bit インデックスの上限）。
		 */
		uint32_t chunkQuadCount = 128;
	};

	/**
	 * @brief チャンク 1 個ぶんの CPU データ。
	 * @details 位置はワールド座標で生成済み（地形はワールド行列を持たない）。UV は持たず、
	 *          シェーダがワールド座標から作る。中身は HeightmapTerrain が持つ
	 *          ➡ span は生成元より長生きさせない。
	 */
	struct TerrainChunkSource
	{
		std::span<const Vector3>  positions;
		std::span<const Vector3>  normals; /**< 高さ配列の中央差分から作った正規化済みの法線。 */
		std::span<const uint16_t> indices;
		Aabb                      bounds; /**< ワールド空間の箱。フラスタムカリングに使う。 */
	};

	/**
	 * @brief R16 ハイトマップを読み、チャンク分割した地形メッシュと高さの問い合わせを提供する。
	 * @details GPU リソースは作らない。DdsImage と同じで、CPU 側のデータを返すところまでがこのクラスの仕事。
	 *          地形は静的なので、読み込み後に中身が変わることはない。高さ配列は GetHeightAt のために
	 *          持ち続ける（接地判定を CPU 側でも使えるようにするため）。
	 * @threading Load / BuildFromHeights はメインスレッドのみ。GetChunks / GetHeightAt / TryGetHeightAt は
	 *            書き込みが無いので、読み込み完了後はどのスレッドから読んでもよい。
	 */
	class HeightmapTerrain
	{
	public:
		FANG_NON_COPYABLE(HeightmapTerrain);

		HeightmapTerrain() = default;

		/**
		 * @brief R16 の DDS を読んでチャンクを生成する。
		 * @param filePath 読み込む .dds の絶対パス。R16 以外の形式はエラーにする。
		 * @param desc     生成条件。
		 * @return 失敗したら false。理由はログに出す。失敗しても落ちず中身は空のままになるので、
		 *         呼び出し側は地形をあきらめて先へ進めばよい。
		 */
		[[nodiscard]] bool Load(const char* filePath, const HeightmapTerrainDesc& desc);

		/**
		 * @brief 高さ配列から直接チャンクを生成する。中身は自分の側へ写す。
		 * @details ファイルを介さずに確かめられるよう、テストからも呼ぶ。Load の後半もここを通る。
		 * @param heights     画素の並び。左上から右へ、行間の詰め物なし。要素数は pixelCountX × pixelCountZ。
		 * @param pixelCountX X 方向の画素数。2 以上であること。
		 * @param pixelCountZ Z 方向の画素数。2 以上であること。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool BuildFromHeights(
			std::span<const uint16_t>   heights,
			uint32_t                    pixelCountX,
			uint32_t                    pixelCountZ,
			const HeightmapTerrainDesc& desc
		);

		/**
		 * @brief 生成したチャンクの列。読めていなければ空。
		 * @details 全クワッドが minHeight で省かれたチャンクはこの列に入らない。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const TerrainChunkSource> GetChunks() const { return m_chunks; }

		/**
		 * @brief ワールド XZ の地表の高さを返す。
		 * @param worldX 問い合わせる X 座標（cm）。地形の範囲外は端へクランプする。
		 * @param worldZ 問い合わせる Z 座標（cm）。
		 * @return 隣接 4 画素のバイリニア補間による高さ（cm）。描画される起伏と同じ値になる。
		 *         読めていなければ 0。
		 */
		[[nodiscard]] float GetHeightAt(float worldX, float worldZ) const;

		/**
		 * @brief ワールド XZ の地表の高さを問い合わせる。範囲外・未読み込みなら false。
		 * @details GetHeightAt と違い端へクランプしない ➡ 地形の外にあるものを端の高さへ黙って吸い寄せない。
		 *          読み込み時にステージの配置を接地させる用途のように、範囲外を「高さが無い」と扱いたい側が使う。
		 * @param worldX    問い合わせる X 座標（cm）。
		 * @param worldZ    問い合わせる Z 座標（cm）。
		 * @param outHeight 範囲内のときだけ高さ（cm）を書く。false のときは触らない。
		 * @return 範囲内で高さを書けたら true。地形の端ちょうどは範囲内として扱う。
		 */
		[[nodiscard]] bool TryGetHeightAt(float worldX, float worldZ, float* outHeight) const;


	private:
		/** @brief チャンクの実体の置き場。TerrainChunkSource の span はここを指す。 */
		struct ChunkGeometry
		{
			std::vector<Vector3>  positions;
			std::vector<Vector3>  normals;
			std::vector<uint16_t> indices;
		};

		/**
		 * @brief 画素空間の座標を隣接 4 画素でバイリニア補間する。
		 * @details GetHeightAt と TryGetHeightAt が範囲の扱いを決めたあと、どちらもここを通る
		 *          ➡ 端へクランプするかどうかだけが違い、補間の結果は同じになる。
		 * @param pixelSpaceX 画素単位の X。0 以上 pixelCountX - 1 以下であること。
		 * @param pixelSpaceZ 画素単位の Z。0 以上 pixelCountZ - 1 以下であること。
		 */
		[[nodiscard]] float SampleHeightAtPixel(float pixelSpaceX, float pixelSpaceZ) const;

		/** @brief 画素の値を cm の高さへ直す。 */
		[[nodiscard]] float PixelToHeight(uint32_t pixelX, uint32_t pixelZ) const;

		/** @brief 高さ配列の中央差分（境界はクランプ）から法線を作る。 */
		[[nodiscard]] Vector3 CalculateNormal(uint32_t pixelX, uint32_t pixelZ) const;

		/** @brief チャンク 1 個ぶんの頂点・法線・インデックス・箱を生成する。 */
		void BuildChunk(uint32_t chunkX, uint32_t chunkZ);

		/** @brief 中身を全部空にする。読み込みに失敗したとき中途半端な中身を残さないため。 */
		void Clear();

		std::vector<uint16_t> m_heights; /**< 画素をそのまま持つ。GetHeightAt のために保持し続ける。 */

		uint32_t m_pixelCountX = 0;
		uint32_t m_pixelCountZ = 0;

		HeightmapTerrainDesc m_desc;

		float m_cellSizeX = 0.0f; /**< 隣の画素とのワールド距離。totalWidth / (pixelCountX - 1)。 */
		float m_cellSizeZ = 0.0f;
		float m_originX   = 0.0f; /**< 画素 (0, 0) のワールド座標。-totalWidth / 2。 */
		float m_originZ   = 0.0f;

		std::vector<ChunkGeometry>      m_chunkGeometries; /**< チャンクの実体。生成後は触らない。 */
		std::vector<TerrainChunkSource> m_chunks;          /**< 実体を指す公開用の列。 */
	};
} // namespace fang
