/**
 * @file SceneRenderer.h
 * @brief View とカリング、フレーム定数の一本化を受け持ち、ShadowPass と ScenePass を RenderGraph へ宣言する。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Aabb.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "RHI/RHIHandles.h"
#include "RHI/RHITypes.h"
#include "Renderer/MeshRenderer.h"
#include "Renderer/RenderGraph.h"
#include <cstdint>
#include <span>


namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang
{
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
	 * @brief SceneRenderer が配った View の番号。
	 * @details AddView が呼ばれた順に振る、1 フレーム限りの通し番号。MeshId と同じ流儀の軽い POD。
	 */
	struct ViewId
	{
		static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

		uint32_t index = INVALID_INDEX; /**< 既定構築と登録失敗では INVALID_INDEX のまま。 */

		/** @brief 登録に成功した番号なら true。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsValid() const { return index != INVALID_INDEX; }
	};

	/**
	 * @brief View ごとにフラスタムカリングとフレーム定数（b1）をまとめ、描画パスを宣言する。
	 * @details View スロットには種別（シーン / シャドウ）があり、シーンは色を、シャドウは光から見た深度だけを
	 *          描く ➡ カスケードで光の View を増やすときもスロットを足すだけで済む。シャドウマップは
	 *          このクラスが所有し、起動時に 1 枚だけ確保する。MeshRenderer は借用ポインタで持つ。
	 *          1 フレームに何度も AddShadowView / AddView / Submit / AddPasses を呼び直す前提で、
	 *          持ち物は View 数（4）ぶんの固定長配列 ➡ 定常状態のヒープ確保は 0。
	 * @threading AddShadowView / AddView / Submit / AddPasses / Reset はメインスレッドのみ。記録は
	 *            RenderGraph のジョブで走り、View ごとに書き込み先（自分の b1 バッファと描画数のスロット）が
	 *            分かれるので競合しない。ShadowPass と ScenePass の記録が並列に走っても、MeshRenderer 側の
	 *            b0 / b2 が Draw と DrawDepth で分かれているので書き込み先は重ならない。
	 *            GetLastDrawnItemCount は Execute の Wait が済んでから読むこと。
	 */
	class SceneRenderer
	{
	public:
		FANG_NON_COPYABLE(SceneRenderer);

		/** @brief 1 フレームに追加できる View の数。 */
		static constexpr uint32_t MAX_VIEW_COUNT = 4;

		/**
		 * @brief シャドウマップの 1 辺のテクセル数。
		 * @details 起動時に 1 枚だけ確保して作り直さないので、画質と 16MB の置き場を釣り合わせた値で固定する。
		 */
		static constexpr uint32_t SHADOW_MAP_SIZE = 2048;

		SceneRenderer() = default;

		/**
		 * @brief View 数ぶんの b1 用動的定数バッファとシャドウマップを確保する。
		 * @param meshRenderer 描画で使うレンダラ。借用ポインタとして持ち続ける。呼び出し側が寿命を保証すること。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(rhi::GraphicsDevice& device, MeshRenderer& meshRenderer);

		/** @brief 確保した定数バッファとシャドウマップを解放する。二重に呼んでも安全。 */
		void Shutdown(rhi::GraphicsDevice& device);

		/** @brief View・Submit の控え・描画数・シャドウの控えをフレームの頭で巻き戻す。 */
		void Reset();

		/**
		 * @brief 光から見た深度だけを描く View を 1 つ追加する。
		 * @param device           光の行列を b1 バッファへ書き込むために使う。
		 * @param directionToLight 面から光源へ向かう向き。正規化して渡すこと。
		 * @param castersBounds    影を落とすものすべてを包むワールド空間の箱。この箱にちょうど合う正射影を組む。
		 * @return View の番号。castersBounds が無効（キャスタが 1 つも無い）か上限に達していたら無効な番号。
		 * @details **AddView より先に呼ぶこと。** シーン View の b1 に光の行列を焼き込むので、順序が逆だと
		 *          そのフレームだけ影が出ない ➡ 逆順で呼ぶとアサートに掛かる。無効な番号が返ったフレームは
		 *          影なしで描かれる（呼び出し側に分岐は要らない）。
		 */
		[[nodiscard]] ViewId AddShadowView(
			rhi::GraphicsDevice& device,
			const Vector3&       directionToLight,
			const Aabb&          castersBounds
		);

		/**
		 * @brief View を 1 つ追加する。
		 * @param device このフレームの視点と光を b1 バッファへ書き込むために使う。
		 * @param view    このフレームのカメラと光。
		 * @return View の番号。上限に達していたら無効な番号（IsValid() が false）。
		 * @details フラスタムの 6 平面を viewProjection から抽出し、その View の b1 バッファへ
		 *          MeshFrameConstants を 1 フレームに 1 回だけ書く。このとき AddShadowView が控えた光の行列と
		 *          影パラメータも一緒に焼き込む。シャドウ View が無いフレームは影パラメータを無効にする
		 *          ➡ 光もシャドウも未設定で落ちない。
		 */
		[[nodiscard]] ViewId AddView(rhi::GraphicsDevice& device, const View& view);

		/**
		 * @brief その View で描くものを控える。
		 * @param view  AddView が返した番号。登録していない番号なら何もしない。
		 * @param items 描くもの。
		 * @details span を控えるだけでコピーしない ➡ 呼び出し側は graph.Execute が戻るまで実体を
		 *          生かしておくこと（この呼び出しの間だけでは足りない）。同じ View へ二度呼ぶと前の控えを上書きする。
		 */
		void Submit(ViewId view, std::span<const RenderItem> items);

		/**
		 * @brief View ごとに ShadowPass と ScenePass を RenderGraph へ宣言する。
		 * @param backBuffer    色の描画先として登録済みのリソース番号。
		 * @param depthBuffer   深度の描画先として登録済みのリソース番号。
		 * @param shadowMap     ImportDepthTexture で登録済みのシャドウマップの番号。ShadowPass の描画先であり、
		 *                      ScenePass が読むリソースでもある（バリアはこの前後関係から Compile が導く）。
		 * @param clearColor    最初のシーン View の色を Clear するときに使う値。loadOperation が Load のときは読まない。
		 * @param loadOperation 最初のシーン View の色・深度に適用する操作。Clear ならその値で塗りつぶし、Load なら
		 *                      前のパスが描いた画の上に重ねる（呼び出し側が別のパスで先に画面を塗っている場合に使う）。
		 * @details 2 つ目以降のシーン View は従来どおり常に Load（前の View が描いたものを残す）。シャドウ View は
		 *          深度だけを描く ShadowPass になり、色の描画先を持たず毎フレーム Clear する。記録はワーカー
		 *          スレッドで走り、Submit した控えを視錐台で絞ってから MeshRenderer へ渡す。
		 */
		void AddPasses(
			RenderGraph&           graph,
			RenderGraphResourceId  backBuffer,
			RenderGraphResourceId  depthBuffer,
			RenderGraphResourceId  shadowMap,
			const rhi::ClearColor& clearColor,
			EnLoadOperation        loadOperation
		);

		/**
		 * @brief 直近の Execute で実際に描いた個数の合計（全 View）。
		 * @details カリングで飛ばした分は数えない。RenderGraph::Execute の Wait が済んでから読むこと。
		 */
		[[nodiscard]] uint32_t GetLastDrawnItemCount() const;

		/**
		 * @brief 所有しているシャドウマップ。
		 * @details 呼び出し側が RenderGraph::ImportDepthTexture へ渡すために取る。所有権は渡さない。
		 */
		[[nodiscard]] FANG_FORCEINLINE rhi::TextureHandle GetShadowMapTexture() const { return m_shadowMap; }


	private:
		/** @brief View スロットの種別。記録で何を描くかがこれで分かれる。 */
		enum class EnViewKind : uint8_t
		{
			Scene,  /**< カメラから見た色。シャドウマップを読んで影を落とす。 */
			Shadow, /**< 光から見た深度だけ。castsShadow なものだけを描く。 */
		};

		/** @brief ScenePass の記録関数に渡す入力。ジョブの中で組み立てずに済むよう POD で揃える。 */
		struct ScenePassRecordArguments
		{
			SceneRenderer*       sceneRenderer = nullptr;
			uint32_t             viewIndex     = 0;
			rhi::GraphicsDevice* device        = nullptr;
		};

		/** @brief ScenePass の記録関数の入口。RenderGraph に渡せるよう関数ポインタの形にしてある。 */
		static void RecordScenePass(void* userData, rhi::CommandList& commandList);

		/**
		 * @brief その View の Submit 控えを視錐台で絞り、生き残った分を MeshRenderer へ渡す。
		 * @details bounds が無効な要素は絞らず常に描く。ヒープ確保はしない（スタック上の固定長配列）。
		 *          シャドウ View は光の視錐台で絞ったうえに castsShadow でも絞り、DrawDepth へ渡す。
		 */
		void RecordView(uint32_t viewIndex, rhi::GraphicsDevice& device, rhi::CommandList& commandList);

		MeshRenderer*        m_meshRenderer = nullptr; /**< 借用ポインタ。所有しない。 */
		rhi::GraphicsDevice* m_device       = nullptr; /**< 借用ポインタ。ScenePass の記録関数へ渡すために持つ。 */

		Frustum m_frustums[MAX_VIEW_COUNT]; /**< AddView / AddShadowView が抽出した 6 平面。View ごと。 */

		rhi::BufferHandle m_frameConstantBuffers[MAX_VIEW_COUNT]; /**< 視点と光（b1）の置き場。View ごと。 */

		EnViewKind m_viewKinds[MAX_VIEW_COUNT] = {}; /**< View スロットごとの種別。 */

		rhi::TextureHandle m_shadowMap; /**< 起動時に 1 枚だけ確保する深度テクスチャ。このクラスが所有する。 */

		/** @brief このフレームの光の viewProjection。シーン View の b1 へも焼き込むので控えておく。 */
		Matrix4x4 m_lightViewProjection;

		/** @brief このフレームにシャドウ View があるか。false ならシーン View の影パラメータを無効にする。 */
		bool m_hasShadowView = false;

		/** @brief Submit が控えた span。実体は呼び出し側が持つ。 */
		std::span<const RenderItem> m_submittedItems[MAX_VIEW_COUNT];

		uint32_t m_drawnItemCounts[MAX_VIEW_COUNT] = {}; /**< RecordView が書く、View ごとの描画数。 */

		ScenePassRecordArguments m_passRecordArguments[MAX_VIEW_COUNT]; /**< AddPasses が埋める userData の実体。 */

		uint32_t m_viewCount = 0;
	};
} // namespace fang
