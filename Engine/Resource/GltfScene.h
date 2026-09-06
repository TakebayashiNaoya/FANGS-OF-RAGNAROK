/**
 * @file GltfScene.h
 * @brief glTF のシーングラフを読み、メッシュの並びと配置の並びを CPU 側の配列として持つ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// cgltf.h はここでは出さない。ノードとプリミティブは前方宣言だけを置き、
// 実体を触る関数の定義は GltfScene.cpp（cgltf.h を include する側）に置く。
struct cgltf_node;
struct cgltf_primitive;
struct cgltf_mesh;


namespace fang
{
	/**
	 * @brief エンジン側メッシュ 1 個分のデータ。glTF のプリミティブ 1 個に対応する。
	 * @details GPU バッファは作らない。頂点の実体は GltfScene が持つ ➡ 返した span はこのオブジェクトより
	 *          長生きさせない。NORMAL / TEXCOORD_0 が無いプリミティブは空の span のまま返す
	 *          （既定値〈法線 (0, 1, 0)、UV (0, 0)〉で埋める側の契約に合わせ、ここでは分岐を持たない）。
	 */
	struct GltfSceneMesh
	{
		std::span<const Vector3>  positions;
		std::span<const Vector3>  normals;
		std::span<const Vector2>  texCoords;
		std::span<const uint16_t> indices;

		/** @brief 頂点の接線（TANGENT）。glTF が持っていなければ空。受け取る側が UV から作る。 */
		std::span<const Vector4> tangents;

		/** @brief マテリアルが指すベースカラー画像のパス。glTF ファイルからの相対。指していなければ空。 */
		std::string_view baseColorImagePath;

		/** @brief マテリアルが指す法線マップ画像のパス。扱いは baseColorImagePath と同じ。 */
		std::string_view normalImagePath;

		/** @brief マテリアルの metallic 係数。0 = 非金属、1 = 金属。書かれていない glTF では既定値の 1.0。 */
		float metallicFactor = 1.0f;

		/** @brief マテリアルの roughness 係数（知覚値）。1 = 粗い面。書かれていない glTF では既定値の 1.0。 */
		float roughnessFactor = 1.0f;

		/** @brief 法線マップの強さ（normalTexture.scale）。書かれていない glTF では既定値の 1.0。 */
		float normalScale = 1.0f;

		/**
		 * @brief glTF の meshes[].name。無ければ空。
		 * @details 実体は GltfScene が持つ ➡ この string_view はこのオブジェクトより長生きさせない。
		 *          同じメッシュの 2 つ目以降のプリミティブも同じ名前を持つ（名前はメッシュ単位、並びはプリミティブ単位）。
		 */
		std::string_view name;
	};

	/**
	 * @brief シーングラフ上の配置 1 個。
	 * @details 同じメッシュを指す配置がいくつあってもよい（meshIndex が同じ配置が複数並ぶ）。
	 */
	struct GltfSceneInstance
	{
		/** @brief GltfScene::GetMeshes() の並びの添字。 */
		size_t meshIndex = 0;

		/** @brief 左手系のワールド行列。ノードのワールド行列（cgltf_node_transform_world）に ConvertToLeftHanded を掛けたもの。 */
		Matrix4x4 world;

		/**
		 * @brief 配置のもとになったノードの名前。ログで場所を指すためだけに持つ。
		 * @details 実体は GltfScene が持つ ➡ 返した string_view はこのオブジェクトより長生きさせない。
		 *          ノードに名前が無ければ空。
		 */
		std::string_view name;
	};

	/**
	 * @brief glTF のシーングラフを読み、メッシュの並びと配置の並びを CPU 側の配列として持つ。
	 * @details 走査は data.scene（無ければ scenes[0]）の根ノードから children を再帰する。
	 *          data.nodes を舐めないのは、どのシーンにも属さないノードまで拾わないため。
	 *          同じプリミティブを指すノードが複数あってもエンジン側メッシュは 1 個で、増えるのは配置だけ
	 *          （対応表の鍵は cgltf_primitive のアドレス）。頂点側は右手系から左手系への Z 反転を
	 *          そのまま踏み、ノードのワールド行列には ConvertToLeftHanded を掛ける
	 *          （2 つの変換が合わさって正しい左手系のワールド座標になる）。
	 *          読めないプリミティブは理由をログに出してそのプリミティブだけを飛ばし、走査は続ける。
	 *          Load が false を返すのはファイルそのものを開けない・解析できない場合だけ。
	 * @threading メインスレッドのみ。
	 */
	class GltfScene
	{
	public:
		FANG_NON_COPYABLE(GltfScene);

		GltfScene() = default;

		/**
		 * @brief glTF を読み、メッシュと配置を自前の配列へ写す。
		 * @details 確保はこの中で終わらせ、以後は配列を動かさない。➡実行中のヒープ確保は起きない。
		 * @param filePath 読み込む .gltf の絶対パス。頂点の実体が外部の .bin にあれば同じ場所から一緒に読む。
		 * @return ファイルを開けない・解析できないときだけ false。メッシュ単位の欠落はログに出して
		 *         残りを返すので、それだけでは false にならない。
		 */
		[[nodiscard]] bool Load(const char* filePath);

		/** @brief 読み込んだメッシュの並び。実体はこのオブジェクトが持つ。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const GltfSceneMesh> GetMeshes() const { return m_meshes; }

		/** @brief 読み込んだ配置の並び。実体はこのオブジェクトが持つ。 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const GltfSceneInstance> GetInstances() const { return m_instances; }


	private:
		/** @brief メッシュ 1 個分の実体。GltfSceneMesh の span / string_view はここを指す。 */
		struct MeshStorage
		{
			std::vector<Vector3>  positions;
			std::vector<Vector3>  normals;
			std::vector<Vector2>  texCoords;
			std::vector<Vector4>  tangents;
			std::vector<uint16_t> indices;
			std::string           baseColorImagePath;
			std::string           normalImagePath;
			float                 metallicFactor  = 1.0f;
			float                 roughnessFactor = 1.0f;
			float                 normalScale     = 1.0f;
			std::string           name;
		};

		/** @brief 配置 1 個分の実体。GltfSceneInstance::name はここの name を指す。 */
		struct InstanceStorage
		{
			size_t      meshIndex = 0;
			Matrix4x4   world;
			std::string name;
		};

		/** @brief 配列を全部空にする。読み込みに失敗したとき中途半端な中身を残さないため。 */
		void Clear();

		/**
		 * @brief ノードとその子孫を再帰的に辿り、メッシュと配置を積む。
		 * @param depth 根ノードを 1 とした深さ。64 を超えたら警告してその枝を捨てる。
		 */
		void TraverseNode(const cgltf_node& node, int depth);

		/**
		 * @brief プリミティブに対応するメッシュ番号を返す。
		 * @details 初めて見るプリミティブなら読み込んで m_meshStorage へ足す。既に見たプリミティブ
		 *          （読み込みに失敗したものも含む）は対応表を引くだけで済ませ、二重に読んだり
		 *          同じ理由のログを何度も出したりしない。
		 * @return 読み込めなかったプリミティブなら INVALID_MESH_INDEX。
		 */
		[[nodiscard]] size_t FindOrAddMesh(
			const cgltf_mesh&      mesh,
			const cgltf_primitive& primitive,
			size_t                 primitiveIndex
		);

		/**
		 * @brief プリミティブ 1 個分の頂点とマテリアルを読む。
		 * @return 65,536 頂点超・三角形リスト以外・インデックス / POSITION 無し・属性ごとに頂点数が違う・
		 *         16bit に収まらないインデックス値、のどれかがあれば理由をログに出して false。
		 */
		[[nodiscard]] bool ReadPrimitiveIntoStorage(
			const cgltf_primitive& primitive,
			const char*            meshName,
			size_t                 primitiveIndex,
			MeshStorage*           outStorage
		) const;

		std::vector<MeshStorage>     m_meshStorage;
		std::vector<InstanceStorage> m_instanceStorage;

		/** @brief cgltf_primitive のアドレス ➡ m_meshStorage の添字。読み込みに一度失敗したプリミティブは
		 *         INVALID_MESH_INDEX を対応させ、同じ理由のログを二度出さないようにする。 */
		std::vector<std::pair<const cgltf_primitive*, size_t>> m_primitiveToMeshIndex;

		/** @brief 読み込みに失敗して捨てたプリミティブの数。起動ログの「捨てた k 個」に使う。 */
		size_t m_discardedMeshCount = 0;

		/** @brief Load が最後に返す並び。span / string_view は全プリミティブを読み終えてから作る
		 *         （読みながら作ると m_meshStorage / m_instanceStorage の伸長でぶら下がりになる）。 */
		std::vector<GltfSceneMesh>     m_meshes;
		std::vector<GltfSceneInstance> m_instances;
	};
} // namespace fang
