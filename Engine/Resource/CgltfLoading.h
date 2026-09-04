/**
 * @file CgltfLoading.h
 * @brief cgltf を使った読み込みで GltfMesh と GltfScene が共有する下ごしらえ。
 * @details Resource モジュール内部専用。cgltf.h を直接使うので、他モジュールへは公開しない
 *          （GltfMesh.h / GltfScene.h には cgltf.h を出さない）。include するのは
 *          GltfMesh.cpp と GltfScene.cpp の 2 つの .cpp だけを想定している。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include <cstdint>
#include <vector>

// 上流のコードは /W4 /WX を想定していないので、この TU の中だけ警告を落とす。
// ThirdParty は改変しない方針なので、抑えるのは取り込む側の責任になる。
#pragma warning(push, 0)
#include "cgltf.h"
#pragma warning(pop)


namespace fang
{
	/** @brief 16bit のインデックスに入る最大値。超える値が来たら黙って切り詰めずエラーにする。 */
	constexpr cgltf_size MAX_INDEX_VALUE = 0xFFFF;

	/** @brief 三角形 1 枚のインデックス数。 */
	constexpr cgltf_size TRIANGLE_INDEX_COUNT = 3;

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
		~CgltfDataHolder();

		/** @brief cgltf_parse_file に渡す受け取り口。 */
		[[nodiscard]] cgltf_data** GetAddressOfData() { return &m_data; }

		/** @brief 読み込んだデータ。まだ読んでいなければ nullptr。 */
		[[nodiscard]] cgltf_data* GetData() const { return m_data; }


	private:
		cgltf_data* m_data = nullptr;
	};

	/**
	 * @brief glTF ファイルを解析し、隣接する .bin まで読み込んで検証する。
	 * @details 非 ASCII パスに対応したファイルコールバック（fang::OpenFile 経由）を組み立てた上で
	 *          cgltf_parse_file / cgltf_load_buffers / cgltf_validate を順に呼ぶ。GltfMesh と
	 *          GltfScene のどちらも同じ手順を踏むので、ここへまとめて置く。
	 * @param filePath 読み込む .gltf の絶対パス。
	 * @param outHolder 読み込んだデータの受け取り先。
	 * @return 失敗したら理由をログに出して false。
	 */
	[[nodiscard]] bool LoadCgltfFile(const char* filePath, CgltfDataHolder* outHolder);

	/**
	 * @brief 属性の並びから欲しい種類のアクセサを探す。
	 * @param setIndex TEXCOORD_0 の 0 にあたる添字。
	 * @return 見つからなければ nullptr。
	 */
	[[nodiscard]] const cgltf_accessor* FindAttributeAccessor(
		const cgltf_primitive& primitive,
		cgltf_attribute_type   attributeType,
		cgltf_int              setIndex
	);

	/**
	 * @brief VEC3 のアクセサを読み、右手系 Y-up から左手系 Y-up へ直して詰める。
	 * @details スケールは掛けない。1 unit = 1cm は読む側が決めた解釈で glTF 側に情報が無いため、
	 *          値をそのまま持てば辻褄が合う。
	 * @return 型が VEC3 でないか、読み出しに失敗したら false。
	 */
	[[nodiscard]] bool ReadVector3Attribute(const cgltf_accessor& accessor, std::vector<Vector3>* outValues);

	/**
	 * @brief VEC4 の TANGENT アクセサを読み、右手系 Y-up から左手系 Y-up へ直して詰める。
	 * @details Z の反転は鏡映（行列式が負）なので、xyz を裏返すだけでは従法線の向きが合わない
	 *          ➡ w（従法線の符号）も一緒に反転する。片方だけだと従法線が裏返り、光を裏返して受ける。
	 * @return 型が VEC4 でないか、読み出しに失敗したら false。
	 */
	[[nodiscard]] bool ReadTangentAttribute(const cgltf_accessor& accessor, std::vector<Vector4>* outValues);

	/**
	 * @brief VEC2 のアクセサを読んで詰める。
	 * @details UV は glTF も D3D も左上原点なので、V は反転しない。
	 * @return 型が VEC2 でないか、読み出しに失敗したら false。
	 */
	[[nodiscard]] bool ReadVector2Attribute(const cgltf_accessor& accessor, std::vector<Vector2>* outValues);

	/**
	 * @brief インデックスを読み、三角形の巻き順を入れ替えて詰める。
	 * @details Z を反転すると三角形の表裏が入れ替わるので、2 個目と 3 個目を交換して元へ戻す。
	 *          これは ReadVector3Attribute の Z 反転とセットで、片方だけだと面が裏返る。
	 * @return SCALAR でない / 3 の倍数でない / 16bit に収まらない値がある、のどれかなら false。
	 */
	[[nodiscard]] bool ReadIndices(const cgltf_accessor& accessor, std::vector<uint16_t>* outIndices);
} // namespace fang
