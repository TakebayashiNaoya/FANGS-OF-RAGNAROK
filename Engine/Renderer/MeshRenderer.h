/**
 * @file MeshRenderer.h
 * @brief インデックス付きのメッシュを単色で描くレンダラ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
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
	 * @brief 1 フレーム分のカメラと平行光。
	 * @details ライトを型でなくばらの値で持つのは、光を書くのは Runtime 側（FrameData）で、
	 *          Renderer はそちらの型を知らないため。呼び出し側が毎フレーム全部を埋めること。
	 */
	struct View
	{
		Matrix4x4 viewProjection; /**< Multiply(ビュー行列, 透視投影行列)。行ベクトル規約なのでビューが左に来る。 */
		Vector3   cameraPosition; /**< ワールドの視点。鏡面反射の視線ベクトル用。 */

		Vector3 directionToLight;      /**< 面から光源へ向かう向き。正規化して渡す。 */
		Vector3 lightColor;            /**< リニア空間の色。 */
		float   lightIntensity = 0.0f; /**< 明るさの倍率。調整は BRDF の式でなくこちらで行う。 */
		Vector3 ambientColor;          /**< 環境項。光の裏側の形を読ませる役。 */
	};

	/**
	 * @brief メッシュ 1 個ぶんの CPU データ。
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

	/** @brief 描くもの 1 個。 */
	struct RenderItem
	{
		MeshId    mesh;  /**< CreateMesh が返した番号。 */
		Matrix4x4 world; /**< モデル座標をワールド座標へ移す行列。 */

		/** @brief ベースカラー。無効なら今までと同じ単色（1×1 のダミー）が差さる。 */
		rhi::TextureHandle baseColor;

		float metallicFactor  = 0.0f; /**< 0 = 非金属。既定はダミーテクスチャのときの見た目を従来に合わせた値。 */
		float roughnessFactor = 1.0f; /**< 知覚 roughness。1 = 粗い面（ハイライトが弱く広い）。 */
	};

	/**
	 * @brief インデックス付きのメッシュを単色で描く。
	 * @details 頂点の並びはシェーダとの契約なので、この中で決めて外へ出さない。
	 *          ビューポートは持たないので、呼び出し側が Draw の前に CommandList::SetViewport を済ませておくこと。
	 * @threading メインスレッドのみ。
	 */
	class MeshRenderer
	{
	public:
		FANG_NON_COPYABLE(MeshRenderer);

		/**
		 * @brief 1 フレームに描けるメッシュの数。
		 * @details 定数バッファの置き場をこの数だけ持つ。1 本を使い回すと、同じフレームの 2 個目が
		 *          1 個目の定数を上書きしてしまう（コマンドはあとでまとめて実行されるため）。
		 *          これを超えたぶんは描かずに警告を出す。増やすのは複数体を出す段の仕事。
		 */
		static constexpr uint32_t MAX_ITEM_COUNT = 4;

		MeshRenderer() = default;

		/**
		 * @brief シェーダとステートの組を作る。深度テストは有効にする。
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
		[[nodiscard]] MeshId CreateMesh(rhi::GraphicsDevice& device, const MeshSource& source);

		/**
		 * @brief 開いているフレームに描画コマンドを積む。
		 * @param device      定数バッファを書き込むために使う。
		 * @param commandList BeginFrame が返したコマンドリスト。
		 * @param items       描くもの。無効な番号の要素は飛ばす。この呼び出しの間だけ読む。
		 * @details Initialize に失敗した状態で呼んでも何もせずに戻る。モデルが出ないだけで、
		 *          ほかの描画は続けられるほうが呼び出し側の分岐が減るため。
		 *          const にしていないのは、定数バッファの置き場を書き換えながら進むため。
		 */
		void Draw(
			rhi::GraphicsDevice&        device,
			rhi::CommandList&           commandList,
			const View&                 view,
			std::span<const RenderItem> items
		);


	private:
		/** @brief メッシュ 1 個ぶんの GPU リソース。 */
		struct Mesh
		{
			rhi::BufferHandle vertexBuffer;   /**< 位置・法線・UV を詰め直した 1 本のバッファ。 */
			rhi::BufferHandle indexBuffer;    /**< 16 bit のインデックス。 */
			uint32_t          indexCount = 0; /**< DrawIndexed に渡す個数。 */
		};

		rhi::PipelineHandle m_pipeline; /**< メッシュ用のシェーダとステートの組。 */
		std::vector<Mesh>   m_meshes;   /**< MeshId.index で引く。捨てないので詰め直しも世代も要らない。 */

		/**
		 * @brief 描くもの 1 個ぶんの定数（MVP・ライト・マテリアル）の置き場。b0 に差す。
		 * @details ルート定数にしないのは、実機のドライバが 16 DWORD 超のルート定数の
		 *          パイプライン生成でデバイスロストするため。
		 */
		rhi::BufferHandle m_objectConstantBuffers[MAX_ITEM_COUNT];

		/** @brief ベースカラーが無いときに差す 1×1。従来の単色と同じ色。 */
		rhi::TextureHandle m_dummyBaseColor;
	};
} // namespace fang
