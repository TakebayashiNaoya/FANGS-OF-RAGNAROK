/**
 * @file MeshRenderer.h
 * @brief 静的メッシュとスキンメッシュを物理ベースライティングで描くレンダラ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/JointIndices.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include "RHI/RHIHandles.h"
#include <cstdint>
#include <span>
#include <vector>


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
	/**
	 * @brief MeshRenderer が配ったメッシュの番号。
	 * @details CreateMesh が呼ばれた順に 0 から振る通し番号。世代付きハンドルの台帳にしないのは、
	 *          メッシュを個別に捨てる手段がまだ無く、スロットの再利用が起きないため。
	 */
	struct MeshId
	{
		static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

		uint32_t index = INVALID_INDEX; /**< 既定構築と生成失敗では INVALID_INDEX のまま。 */

		/** @brief 生成に成功した番号なら true。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsValid() const { return index != INVALID_INDEX; }
	};

	/**
	 * @brief 静的メッシュ 1 個ぶんの CPU データ。
	 * @details 並びのばらばらな配列で受け取り、CreateMesh が内部の頂点形式へ詰め直す。
	 *          positions と indices は必須で、どちらかが空なら生成に失敗する。
	 *          normals と texCoords は空でもよく、空なら既定値（法線は (0, 1, 0)、UV は (0, 0)）で埋める。
	 *          空でないなら要素数が positions と同じであること。違えば生成に失敗する。
	 *          インデックスが 16 bit なので頂点は 65,536 個まで。
	 */
	struct MeshSource
	{
		std::span<const Vector3>  positions;
		std::span<const Vector3>  normals;
		std::span<const Vector2>  texCoords;
		std::span<const uint16_t> indices;
	};

	/**
	 * @brief スキンメッシュ 1 個ぶんの CPU データ。
	 * @details 静的メッシュ（MeshSource）に関節の番号と重みが加わったもの。すべて必須で、
	 *          要素数は positions と同じであること。違えば生成に失敗する。
	 *          インデックスが 16 bit なので頂点は 65,536 個まで。
	 */
	struct SkinnedMeshSource
	{
		std::span<const Vector3>      positions;
		std::span<const Vector3>      normals;
		std::span<const Vector2>      texCoords;
		std::span<const uint16_t>     indices;
		std::span<const JointIndices> jointIndices;
		std::span<const Vector4>      jointWeights;
	};

	/**
	 * @brief 描くもの 1 個。
	 * @details 静的もスキンもこの 1 本で表す。どちらとして描くかは番号を配った台帳が覚えているので、
	 *          呼び出し側が指定するものではない。
	 */
	struct RenderItem
	{
		MeshId    mesh;  /**< CreateMesh が返した番号。 */
		Matrix4x4 world; /**< モデル座標をワールド座標へ移す行列。 */

		/**
		 * @brief ワールド空間の境界ボックス。GetLocalBounds を world で変換したもの。
		 * @details 既定の無効な箱（Aabb::IsValid() が false）を渡すと「常に描く」扱いになる
		 *          ➡ 箱を持たないものでも呼び出し側に分岐が要らない。
		 */
		Aabb bounds;

		/**
		 * @brief 関節ごとのスキニング行列。並びはメッシュの関節番号と同じ。
		 * @details この呼び出しの間だけ読む。空なら変形せずバインドポーズで描く
		 *          ➡ 読み込みに失敗したときも呼び出し側に分岐が要らない。
		 *          静的メッシュの番号を指しているときは読まれない。
		 */
		std::span<const Matrix4x4> skinningMatrices;

		/** @brief ベースカラー。無効なら今までと同じ単色（1×1 のダミー）が差さる。 */
		rhi::TextureHandle baseColor;

		float metallicFactor  = 0.0f; /**< 0 = 非金属。既定はダミーテクスチャのときの見た目を従来に合わせた値。 */
		float roughnessFactor = 1.0f; /**< 知覚 roughness。1 = 粗い面（ハイライトが弱く広い）。 */
	};

	/**
	 * @brief 静的メッシュとスキンメッシュを描く。
	 * @details 頂点の並びはシェーダとの契約なので、この中で決めて外へ出さない。静的とスキンは頂点形式も
	 *          パイプラインも別だが、台帳・定数の組み立て・ダミーテクスチャは同じなので 1 クラスに畳んである。
	 *          ビューポートは持たないので、呼び出し側が Draw の前に CommandList::SetViewport を済ませておくこと。
	 * @threading メインスレッドのみ。
	 */
	class MeshRenderer
	{
	public:
		FANG_NON_COPYABLE(MeshRenderer);

		/**
		 * @brief シェーダが持つ骨行列の本数。狼は 59 なので余白がある。
		 * @details SkinnedMeshVS.hlsl の MAX_JOINT_COUNT と対。片方だけ変えると読み書きの範囲がずれる。
		 */
		static constexpr uint32_t MAX_JOINT_COUNT = 64;

		/**
		 * @brief 1 フレームに描けるメッシュの数。
		 * @details 定数バッファの置き場をこの数だけ持つ。1 本を使い回すと、同じフレームの 2 個目が
		 *          1 個目の定数を上書きしてしまう（コマンドはあとでまとめて実行されるため）。
		 *          これを超えたぶんは描かずに警告を出す。増やすのは複数体を出す段の仕事。
		 */
		static constexpr uint32_t MAX_ITEM_COUNT = 4;

		MeshRenderer() = default;

		/**
		 * @brief 静的用とスキン用のシェーダとステートの組、定数の置き場を作る。深度テストは有効にする。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device);

		/** @brief パイプラインと作ったメッシュを全部解放する。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/**
		 * @brief 静的メッシュの頂点バッファとインデックスバッファを作る。
		 * @param source 元データ。この呼び出しの間だけ読むので、戻ったら破棄してよい。
		 * @return 失敗したら無効な番号（IsValid() が false）。呼び出し側はそのメッシュを描かずに飛ばす。
		 */
		[[nodiscard]] MeshId CreateMesh(rhi::GraphicsDevice& device, const MeshSource& source);

		/**
		 * @brief スキンメッシュの頂点バッファとインデックスバッファを作る。
		 * @param source 元データ。この呼び出しの間だけ読むので、戻ったら破棄してよい。
		 * @return 失敗したら無効な番号（IsValid() が false）。呼び出し側はそのメッシュを描かずに飛ばす。
		 */
		[[nodiscard]] MeshId CreateMesh(rhi::GraphicsDevice& device, const SkinnedMeshSource& source);

		/**
		 * @brief メッシュのモデル空間の境界ボックスを返す。
		 * @param mesh CreateMesh が返した番号。
		 * @return 位置から作った箱。作った覚えのない番号なら無効な箱（Aabb::IsValid() が false）。
		 * @details スキンメッシュはバインドポーズのままの箱。姿勢で頂点がはみ出すのが見えたら余白を足す。
		 */
		[[nodiscard]] Aabb GetLocalBounds(MeshId mesh) const;

		/**
		 * @brief 開いているフレームに描画コマンドを積む。
		 * @param device              定数バッファを書き込むために使う。
		 * @param commandList         BeginFrame が返したコマンドリスト。
		 * @param frameConstantBuffer b1 に差す視点と光の定数バッファ。中身は呼び出し側が先に書いておくこと。
		 * @param items               描くもの。無効な番号の要素は飛ばす。この呼び出しの間だけ読む。
		 * @details Initialize に失敗した状態で呼んでも何もせずに戻る。モデルが出ないだけで、
		 *          ほかの描画は続けられるほうが呼び出し側の分岐が減るため。カリングはしない
		 *          ➡ 視錐台で絞るのは呼び出し側（SceneRenderer）の仕事。
		 *          const にしていないのは、定数バッファの置き場を書き換えながら進むため。
		 */
		void Draw(
			rhi::GraphicsDevice&        device,
			rhi::CommandList&           commandList,
			rhi::BufferHandle           frameConstantBuffer,
			std::span<const RenderItem> items
		);


	private:
		/** @brief メッシュ 1 個ぶんの GPU リソースと、番号を引くのに要る素性。 */
		struct Mesh
		{
			rhi::BufferHandle vertexBuffer;      /**< 位置・法線・UV（スキンなら関節と重みも）を詰め直した 1 本。 */
			rhi::BufferHandle indexBuffer;       /**< 16 bit のインデックス。 */
			uint32_t          indexCount = 0;    /**< DrawIndexed に渡す個数。 */
			Aabb              localBounds;       /**< モデル空間の箱。CreateMesh が位置から作る。 */
			bool              isSkinned = false; /**< true ならスキン用のパイプラインと頂点形式で描く。 */
		};

		/**
		 * @brief 頂点とインデックスを GPU へ載せて台帳に登録する。静的とスキンで共通の後半。
		 * @param vertices      詰め直し済みの頂点列の先頭。
		 * @param sizeInBytes   vertices の総バイト数。
		 * @param strideInBytes 頂点 1 個のバイト数。
		 * @return 失敗したら無効な番号。途中で失敗したぶんはこの中で解放する。
		 */
		[[nodiscard]] MeshId RegisterMesh(
			rhi::GraphicsDevice&      device,
			const void*               vertices,
			uint32_t                  sizeInBytes,
			uint32_t                  strideInBytes,
			std::span<const uint16_t> indices,
			const Aabb&               localBounds,
			bool                      isSkinned
		);

		rhi::PipelineHandle m_staticPipeline;  /**< 静的メッシュ用のシェーダとステートの組。 */
		rhi::PipelineHandle m_skinnedPipeline; /**< スキンメッシュ用。頂点形式と頂点シェーダーだけが違う。 */

		std::vector<Mesh> m_meshes; /**< MeshId.index で引く。捨てないので詰め直しも世代も要らない。 */

		/**
		 * @brief 描くもの 1 個ぶんの定数（world・材質）の置き場。b0 に差す。
		 * @details ルート定数にしないのは、実機のドライバが 16 DWORD 超のルート定数の
		 *          パイプライン生成でデバイスロストするため。
		 */
		rhi::BufferHandle m_objectConstantBuffers[MAX_ITEM_COUNT];

		/** @brief 骨行列の置き場。b2 に差す。スキンメッシュを 1 個描くごとに 1 本使う。 */
		rhi::BufferHandle m_skinningConstantBuffers[MAX_ITEM_COUNT];

		/** @brief ベースカラーが無いときに差す 1×1。従来の単色と同じ色。 */
		rhi::TextureHandle m_dummyBaseColor;
	};
} // namespace fang
