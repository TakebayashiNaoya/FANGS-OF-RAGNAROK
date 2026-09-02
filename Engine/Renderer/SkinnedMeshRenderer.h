/**
 * @file SkinnedMeshRenderer.h
 * @brief 骨に追従して変形するメッシュを描くレンダラ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/JointIndices.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include "RHI/RHIHandles.h"
#include "Renderer/MeshRenderer.h"
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

	/** @brief 描くもの 1 個。 */
	struct SkinnedRenderItem
	{
		MeshId    mesh;  /**< CreateMesh が返した番号。 */
		Matrix4x4 world; /**< モデル座標をワールド座標へ移す行列。 */

		/**
		 * @brief 関節ごとのスキニング行列。並びはメッシュの関節番号と同じ。
		 * @details この呼び出しの間だけ読む。空なら変形せずバインドポーズで描く
		 *          ➡ 読み込みに失敗したときも呼び出し側に分岐が要らない。
		 */
		std::span<const Matrix4x4> skinningMatrices;

		/** @brief ベースカラー。無効なら今までと同じ単色（1×1 のダミー）が差さる。 */
		rhi::TextureHandle baseColor;

		float metallicFactor  = 0.0f; /**< 0 = 非金属。既定はダミーテクスチャのときの見た目を従来に合わせた値。 */
		float roughnessFactor = 1.0f; /**< 知覚 roughness。1 = 粗い面（ハイライトが弱く広い）。 */
	};

	/**
	 * @brief 骨の重みを持つメッシュを、渡されたスキニング行列で変形して描く。
	 * @details 頂点の並びはシェーダとの契約なので、この中で決めて外へ出さない。
	 *          静的メッシュとは頂点形式もパイプラインも別なので、MeshRenderer とは並べて置く。
	 *          ビューポートは持たないので、呼び出し側が Draw の前に CommandList::SetViewport を済ませておくこと。
	 * @threading メインスレッドのみ。
	 */
	class SkinnedMeshRenderer
	{
	public:
		FANG_NON_COPYABLE(SkinnedMeshRenderer);

		/**
		 * @brief シェーダが持つ骨行列の本数。狼は 59 なので余白がある。
		 * @details SkinnedMeshVS.hlsl の MAX_JOINT_COUNT と対。片方だけ変えると読み書きの範囲がずれる。
		 */
		static constexpr uint32_t MAX_JOINT_COUNT = 64;

		/**
		 * @brief 1 フレームに描けるスキンメッシュの数。
		 * @details 骨行列の置き場をこの数だけ持つ。1 本を使い回すと、同じフレームの 2 体目が
		 *          1 体目の行列を上書きしてしまう（コマンドはあとでまとめて実行されるため）。
		 *          これを超えたぶんは描かずに警告を出す。増やすのは複数体を出す段の仕事。
		 */
		static constexpr uint32_t MAX_ITEM_COUNT = 4;

		SkinnedMeshRenderer() = default;

		/**
		 * @brief シェーダとステートの組、骨行列の置き場を作る。深度テストは有効にする。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device);

		/** @brief パイプラインと作ったメッシュを全部解放する。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/**
		 * @brief 頂点バッファとインデックスバッファを作る。
		 * @param source 元データ。この呼び出しの間だけ読むので、戻ったら破棄してよい。
		 * @return 失敗したら無効な番号（IsValid() が false）。呼び出し側はそのメッシュを描かずに飛ばす。
		 */
		[[nodiscard]] MeshId CreateMesh(rhi::GraphicsDevice& device, const SkinnedMeshSource& source);

		/**
		 * @brief 開いているフレームに描画コマンドを積む。
		 * @param device      骨行列を書き込むために使う。
		 * @param commandList BeginFrame が返したコマンドリスト。
		 * @param items       描くもの。無効な番号の要素は飛ばす。この呼び出しの間だけ読む。
		 * @details Initialize に失敗した状態で呼んでも何もせずに戻る。モデルが出ないだけで、
		 *          ほかの描画は続けられるほうが呼び出し側の分岐が減るため。
		 *          const にしていないのは、骨行列の置き場を書き換えながら進むため。
		 */
		void Draw(
			rhi::GraphicsDevice&               device,
			rhi::CommandList&                  commandList,
			const View&                        view,
			std::span<const SkinnedRenderItem> items
		);


	private:
		/** @brief メッシュ 1 個ぶんの GPU リソース。 */
		struct Mesh
		{
			rhi::BufferHandle vertexBuffer;   /**< 位置・法線・UV・関節番号・重みを詰め直した 1 本のバッファ。 */
			rhi::BufferHandle indexBuffer;    /**< 16 bit のインデックス。 */
			uint32_t          indexCount = 0; /**< DrawIndexed に渡す個数。 */
		};

		rhi::PipelineHandle m_pipeline; /**< スキンメッシュ用のシェーダとステートの組。 */
		std::vector<Mesh>   m_meshes;   /**< MeshId.index で引く。捨てないので詰め直しも世代も要らない。 */

		/** @brief 骨行列の置き場。b1 に差す。描くもの 1 個につき 1 本使う。 */
		rhi::BufferHandle m_jointMatrixBuffers[MAX_ITEM_COUNT];

		/** @brief ベースカラーが無いときに差す 1×1。従来の単色と同じ色。 */
		rhi::TextureHandle m_dummyBaseColor;
	};
} // namespace fang
