/**
 * @file GltfMesh.h
 * @brief glTF から取り出したメッシュ 1 個分の頂点とインデックス。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/JointIndices.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>


namespace fang
{
	/**
	 * @brief glTF のメッシュと、それに紐づく骨の情報を CPU 側の配列として持つ。
	 * @details GPU バッファは作らない。頂点の並びはシェーダとの契約なので、詰め方は受け取った側が決める。
	 *          読み込むときに右手系 Y-up の glTF を左手系 Y-up へ直す（Z の反転と巻き順の入れ替え）。
	 *          メッシュやプリミティブが複数ある glTF は先頭の 1 個だけを読み、残りは警告に出して捨てる。
	 *          エラーにしないのは、書き出しの設定でメッシュが分かれたときも表示を続けたいため。
	 *          骨の情報を同じクラスで持つのは、`Wolf.gltf` 6MB + `Wolf.bin` 15MB を 2 回解析しないため。
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

		/**
		 * @brief 頂点の接線（TANGENT）。xyz = 接線、w = 従法線の符号。
		 * @details glTF が持っていなければ空。受け取る側が UV から作る契約なので、無くてもエラーにしない。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const Vector4> GetTangents() const { return m_tangents; }

		/** @brief 三角形リストのインデックス。3 個で 1 三角形。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const uint16_t> GetIndices() const { return m_indices; }

		/**
		 * @brief 骨の情報が揃っているか。
		 * @details 揃っていなければ下の 4 つはすべて空になる。スキンの無い glTF でも読み込みは成功するので、
		 *          呼び出し側はこれを見てスキンメッシュか静的メッシュかを決める。
		 */
		[[nodiscard]] FANG_FORCEINLINE bool HasSkin() const { return !m_inverseBindMatrices.empty(); }

		/** @brief 頂点が影響を受ける関節の番号。位置と同じ数だけある。番号は GetJointNames() の並び。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const JointIndices> GetJointIndices() const { return m_jointIndices; }

		/** @brief 関節ごとの重み。位置と同じ数だけあり、4 成分の合計は 1 に正規化してある。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const Vector4> GetJointWeights() const { return m_jointWeights; }

		/**
		 * @brief バインドポーズを打ち消す行列。関節の数だけある。
		 * @details 左手系へ直し済みなので、そのままスキニング行列の左側に掛けられる。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const Matrix4x4> GetInverseBindMatrices() const
		{
			return m_inverseBindMatrices;
		}

		/**
		 * @brief 関節の名前。glTF の skin.joints の並び。
		 * @details ozz は関節を並べ替えるので、姿勢を受け取る側はこの名前で対応表を作る。
		 *          文字列の実体はこのクラスが持つ ➡ 返した span はこのオブジェクトより長生きさせない。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const char* const> GetJointNames() const { return m_jointNames; }

		/**
		 * @brief マテリアルが指すベースカラー画像のパス。glTF ファイルからの相対。
		 * @details 画像を指していないモデルでは空。パーセント符号は戻してある。
		 *          区切りは glTF に書かれたまま（ふつうは /）なので、繋ぐ側で \ に直すこと。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::string_view GetBaseColorImagePath() const { return m_baseColorImagePath; }

		/** @brief マテリアルの metallic 係数。0 = 非金属、1 = 金属。書かれていない glTF では既定値の 1.0。 */
		[[nodiscard]] FANG_FORCEINLINE float GetMetallicFactor() const { return m_metallicFactor; }

		/** @brief マテリアルの roughness 係数（知覚値）。1 = 粗い面。書かれていない glTF では既定値の 1.0。 */
		[[nodiscard]] FANG_FORCEINLINE float GetRoughnessFactor() const { return m_roughnessFactor; }

		/**
		 * @brief マテリアルが指す法線マップ画像のパス。glTF ファイルからの相対。
		 * @details 指していなければ空。扱いは GetBaseColorImagePath と同じ。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::string_view GetNormalImagePath() const { return m_normalImagePath; }

		/** @brief 法線マップの強さ（normalTexture.scale）。書かれていない glTF では既定値の 1.0。 */
		[[nodiscard]] FANG_FORCEINLINE float GetNormalScale() const { return m_normalScale; }


	private:
		/** @brief 配列を全部空にする。読み込みに失敗したとき中途半端な中身を残さないため。 */
		void Clear();

		std::vector<Vector3>  m_positions;
		std::vector<Vector3>  m_normals;
		std::vector<Vector2>  m_texCoords;
		std::vector<Vector4>  m_tangents; /**< glTF が TANGENT を持っていなければ空。 */
		std::vector<uint16_t> m_indices;

		std::vector<JointIndices> m_jointIndices;
		std::vector<Vector4>      m_jointWeights;
		std::vector<Matrix4x4>    m_inverseBindMatrices;

		/** @brief 関節名の実体。cgltf のデータは Load を抜けるところで解放するので写しを持つ。 */
		std::vector<std::string> m_jointNameStorage;

		/** @brief m_jointNameStorage を指すポインタ列。span で返すために別に持つ。 */
		std::vector<const char*> m_jointNames;

		/** @brief ベースカラー画像の相対パス。指していなければ空。 */
		std::string m_baseColorImagePath;

		/** @brief 法線マップ画像の相対パス。指していなければ空。 */
		std::string m_normalImagePath;

		/** @brief マテリアルの係数。glTF の既定値で初期化し、書かれていれば Load が上書きする。 */
		float m_metallicFactor  = 1.0f;
		float m_roughnessFactor = 1.0f;
		float m_normalScale     = 1.0f;
	};
} // namespace fang
