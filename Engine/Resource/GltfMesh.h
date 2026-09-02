/**
 * @file GltfMesh.h
 * @brief glTF から取り出したメッシュ 1 個分の頂点とインデックス。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include <cstdint>
#include <span>
#include <vector>


namespace fang
{
	/**
	 * @brief glTF のメッシュを CPU 側の配列として持つ。
	 * @details GPU バッファは作らない。頂点の並びはシェーダとの契約なので、詰め方は受け取った側が決める。
	 *          読み込むときに右手系 Y-up の glTF を左手系 Y-up へ直す（Z の反転と巻き順の入れ替え）。
	 *          メッシュやプリミティブが複数ある glTF は先頭の 1 個だけを読み、残りは警告に出して捨てる。
	 *          エラーにしないのは、書き出しの設定でメッシュが分かれたときも表示を続けたいため。
	 * @threading メインスレッドのみ。
	 */
	class GltfMesh
	{
	public:
		FANG_NON_COPYABLE(GltfMesh);

		GltfMesh() = default;

		/**
		 * @brief glTF を読み、頂点とインデックスを自前の配列へ写す。
		 * @details 確保はこの中で終わらせ、以後は配列を動かさない。➡実行中のヒープ確保は起きない。
		 * @param filePath 読み込む .gltf の絶対パス。頂点の実体が外部の .bin にあれば同じ場所から一緒に読む。
		 * @return 失敗したら false。理由はログに出す。失敗しても落ちず中身は空のままになるので、
		 *         呼び出し側は描画をあきらめて先へ進めばよい。
		 */
		[[nodiscard]] bool Load(const char* filePath);

		/** @brief 頂点の位置。単位は 1 = 1cm と解釈している（glTF 側に単位の情報は無い）。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const Vector3> GetPositions() const { return m_positions; }

		/** @brief 頂点の法線。位置と同じ数だけある。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const Vector3> GetNormals() const { return m_normals; }

		/** @brief 頂点のテクスチャ座標（TEXCOORD_0）。位置と同じ数だけある。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const Vector2> GetTexCoords() const { return m_texCoords; }

		/** @brief 三角形リストのインデックス。3 個で 1 三角形。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const uint16_t> GetIndices() const { return m_indices; }


	private:
		/** @brief 配列を全部空にする。読み込みに失敗したとき中途半端な中身を残さないため。 */
		void Clear();

		std::vector<Vector3>  m_positions;
		std::vector<Vector3>  m_normals;
		std::vector<Vector2>  m_texCoords;
		std::vector<uint16_t> m_indices;
	};
} // namespace fang
