/**
 * @file SceneRenderer.cpp
 * @brief View・カリング・フレーム定数をまとめ、ShadowPass と ScenePass を記録する実装。
 */
#include "Pch.h"
#include "Renderer/SceneRenderer.h"
#include "Core/Log/Assert.h"
#include "RHI/CommandList.h"
#include "RHI/GraphicsDevice.h"
#include "Renderer/RendererLog.h"
#include "Renderer/Shaders/MeshConstants.h"


namespace fang
{
	namespace
	{
		/** @brief 視錐台カリングを生き延びた分を積む作業領域の上限。 */
		constexpr uint32_t MAX_CULLED_ITEM_COUNT = 16;

		/** @brief シャドウマップ 1 テクセルの UV 幅。PCF がずらす量の基準になる。 */
		constexpr float SHADOW_MAP_TEXEL_SIZE = 1.0f / static_cast<float>(SceneRenderer::SHADOW_MAP_SIZE);

		/**
		 * @brief 光が真上・真下を向いていると見なす |y| の境目。
		 * @details ここを超えると既定の上方向（+Y）が視線とほぼ平行になり、MakeLookAtMatrix が基底を作れない。
		 */
		constexpr float SHADOW_UP_AXIS_LIMIT = 0.99f;

		/**
		 * @brief 光の視点をキャスタの箱から離す余白。cm。
		 * @details 箱の対角長に足して視点が箱の中へ入らないようにする。正射影なので離しても絵の大きさは変わらない。
		 */
		constexpr float SHADOW_EYE_MARGIN_CENTIMETERS = 100.0f;

		/**
		 * @brief 正射影の far をキャスタの奥へ延ばす量。cm。
		 * @details キャスタの真下にある床を光のフラスタムへ入れて、影を受けさせるため。
		 */
		constexpr float SHADOW_FAR_EXTENSION_CENTIMETERS = 500.0f;

		/**
		 * @brief キャスタの箱にちょうど合う光の viewProjection を組む。
		 * @param directionToLight 面から光源へ向かう向き（正規化済み）。
		 * @param castersBounds    影を落とすものすべてを包むワールド空間の箱。有効であること。
		 * @details 8 頂点を光空間へ移して min/max を取るので、事前計算なしで毎フレーム範囲を合わせ直せる。
		 */
		[[nodiscard]] Matrix4x4 MakeLightViewProjection(const Vector3& directionToLight, const Aabb& castersBounds)
		{
			const Vector3 boxCenter         = (castersBounds.min + castersBounds.max) * 0.5f;
			const float   boxDiagonalLength = Length(castersBounds.max - castersBounds.min);

			// 光が真上・真下を向いているフレームだけ上方向に Z 軸を使う。+Y のままだと視線と平行で基底が作れない。
			const Vector3 upDirection = (std::fabs(directionToLight.y) > SHADOW_UP_AXIS_LIMIT)
											? Vector3{ 0.0f, 0.0f, 1.0f }
											: Vector3{ 0.0f, 1.0f, 0.0f };

			const Vector3 eye = boxCenter + directionToLight * (boxDiagonalLength + SHADOW_EYE_MARGIN_CENTIMETERS);

			const Matrix4x4 lightView = MakeLookAtMatrix(eye, boxCenter, upDirection);

			Vector3 corners[8];
			castersBounds.GetCorners(corners);

			Aabb lightSpaceBounds;
			for (const Vector3& corner : corners)
			{
				lightSpaceBounds.Expand(TransformPoint(corner, lightView));
			}

			const Matrix4x4 lightProjection = MakeOrthographicOffCenterMatrix(
				lightSpaceBounds.min.x,
				lightSpaceBounds.max.x,
				lightSpaceBounds.min.y,
				lightSpaceBounds.max.y,
				lightSpaceBounds.min.z,
				lightSpaceBounds.max.z + SHADOW_FAR_EXTENSION_CENTIMETERS
			);

			return Multiply(lightView, lightProjection);
		}

		/**
		 * @brief フレームの間ずっと同じ定数を組む。行列は行優先のまま転置しない（MeshRenderer と同じ流儀）。
		 * @param lightViewProjection 影の判定に使う光の行列。isShadowEnabled が false なら読まれない。
		 * @param isShadowEnabled     このフレームにシャドウ View があるか。false なら影係数を 1 に固定させる。
		 */
		[[nodiscard]] MeshFrameConstants MakeFrameConstants(
			const View&      view,
			const Matrix4x4& lightViewProjection,
			bool             isShadowEnabled
		)
		{
			const Vector3 cameraPosition = view.cameraPosition;
			const Vector3 lightDirection = view.directionToLight;
			const Vector3 lightColor     = view.lightColor;
			const Vector3 ambientColor   = view.ambientColor;

			MeshFrameConstants constants{};
			constants.viewProjection = view.viewProjection;

			constants.cameraPosition   = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f };
			constants.directionToLight = { lightDirection.x, lightDirection.y, lightDirection.z, 0.0f };
			constants.lightColor       = { lightColor.x, lightColor.y, lightColor.z, view.lightIntensity };
			constants.ambientColor     = { ambientColor.x, ambientColor.y, ambientColor.z, 0.0f };

			constants.lightViewProjection = lightViewProjection;
			constants.shadowParameters    = { SHADOW_MAP_TEXEL_SIZE, isShadowEnabled ? 1.0f : 0.0f, 0.0f, 0.0f };

			return constants;
		}
	} // namespace


	bool SceneRenderer::Initialize(rhi::GraphicsDevice& device, MeshRenderer& meshRenderer)
	{
		m_device       = &device;
		m_meshRenderer = &meshRenderer;

		for (rhi::BufferHandle& buffer : m_frameConstantBuffers)
		{
			buffer = device.CreateDynamicBuffer(sizeof(MeshFrameConstants), 0, rhi::EnBufferKind::Constant);
			if (!buffer.IsValid())
			{
				return false;
			}
		}

		// 作り直す仕組みを持たないので、ここで 1 枚だけ確保して終わりにする。
		m_shadowMap = device.CreateDepthTexture(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
		if (!m_shadowMap.IsValid())
		{
			return false;
		}

		FANG_LOG_INFO(Renderer, "シーン描画の準備ができた");

		return true;
	}


	void SceneRenderer::Shutdown(rhi::GraphicsDevice& device)
	{
		for (rhi::BufferHandle& buffer : m_frameConstantBuffers)
		{
			device.DestroyBuffer(buffer);
			buffer = {};
		}

		device.DestroyTexture(m_shadowMap);
		m_shadowMap = {};

		m_meshRenderer = nullptr;
		m_device       = nullptr;
	}


	void SceneRenderer::Reset()
	{
		m_viewCount = 0;

		for (uint32_t viewIndex = 0; viewIndex < MAX_VIEW_COUNT; ++viewIndex)
		{
			m_submittedItems[viewIndex]  = {};
			m_drawnItemCounts[viewIndex] = 0;
			m_viewKinds[viewIndex]       = EnViewKind::Scene;
		}

		m_lightViewProjection = Matrix4x4{};
		m_hasShadowView       = false;
	}


	ViewId SceneRenderer::AddShadowView(
		rhi::GraphicsDevice& device,
		const Vector3&       directionToLight,
		const Aabb&          castersBounds
	)
	{
		// シーン View の b1 へ光の行列を焼き込むので、シャドウ View はそれより先に登録されていないといけない。
		for (uint32_t registeredIndex = 0; registeredIndex < m_viewCount; ++registeredIndex)
		{
			FANG_ASSERT(
				m_viewKinds[registeredIndex] == EnViewKind::Shadow,
				"シーン View を登録した後にシャドウ View を足そうとしている"
			);
		}

		// キャスタが 1 つも無いフレーム。落とす影が無いので View ごと作らず、影なしで描かせる。
		if (!castersBounds.IsValid())
		{
			return ViewId{};
		}

		FANG_ASSERT(m_viewCount < MAX_VIEW_COUNT, "1 フレームに追加できる View を使い切った");

		if (m_viewCount >= MAX_VIEW_COUNT)
		{
			return ViewId{};
		}

		const uint32_t viewIndex = m_viewCount;
		++m_viewCount;

		m_viewKinds[viewIndex] = EnViewKind::Shadow;

		const Matrix4x4 lightViewProjection = MakeLightViewProjection(directionToLight, castersBounds);

		// 正射影でも平面の取り出し方は透視投影と同じ式なので、カリングは既存の経路にそのまま乗る。
		m_frustums[viewIndex].ExtractFromViewProjection(lightViewProjection);

		// 深度しか書かないので視点も光の色も読まれない。値の入る場所を光の行列だけに絞る。
		const View lightView{ .viewProjection = lightViewProjection };

		const MeshFrameConstants frameConstants = MakeFrameConstants(lightView, lightViewProjection, true);
		device.UpdateBuffer(m_frameConstantBuffers[viewIndex], &frameConstants, sizeof(frameConstants));

		m_lightViewProjection = lightViewProjection;
		m_hasShadowView       = true;

		return ViewId{ .index = viewIndex };
	}


	ViewId SceneRenderer::AddView(rhi::GraphicsDevice& device, const View& view)
	{
		FANG_ASSERT(m_viewCount < MAX_VIEW_COUNT, "1 フレームに追加できる View を使い切った");

		if (m_viewCount >= MAX_VIEW_COUNT)
		{
			return ViewId{};
		}

		const uint32_t viewIndex = m_viewCount;
		++m_viewCount;

		m_viewKinds[viewIndex] = EnViewKind::Scene;

		m_frustums[viewIndex].ExtractFromViewProjection(view.viewProjection);

		// 視点と光は描画物が変わっても変わらないので、この View に対しては 1 フレームに 1 回だけ書く。
		const MeshFrameConstants frameConstants = MakeFrameConstants(view, m_lightViewProjection, m_hasShadowView);
		device.UpdateBuffer(m_frameConstantBuffers[viewIndex], &frameConstants, sizeof(frameConstants));

		return ViewId{ .index = viewIndex };
	}


	void SceneRenderer::Submit(ViewId view, std::span<const RenderItem> items)
	{
		if (!view.IsValid() || view.index >= m_viewCount)
		{
			FANG_ASSERT(false, "登録していない View へ Submit しようとしている");
			return;
		}

		m_submittedItems[view.index] = items;
	}


	void SceneRenderer::AddPasses(
		RenderGraph&           graph,
		RenderGraphResourceId  backBuffer,
		RenderGraphResourceId  depthBuffer,
		RenderGraphResourceId  shadowMap,
		const rhi::ClearColor& clearColor,
		EnLoadOperation        loadOperation
	)
	{
		// シャドウ View は AddView より先に登録される契約なので、宣言もこの並び順のまま
		// ShadowPass ➡ ScenePass になる。あとは Compile がその前後関係からバリアを導く。
		bool isFirstScenePass = true;

		for (uint32_t viewIndex = 0; viewIndex < m_viewCount; ++viewIndex)
		{
			m_passRecordArguments[viewIndex] = ScenePassRecordArguments{
				.sceneRenderer = this,
				.viewIndex     = viewIndex,
				.device        = m_device,
			};

			RenderGraphPassDesc passDesc{};
			passDesc.recordThread = EnPassRecordThread::Job;
			passDesc.record       = &SceneRenderer::RecordScenePass;
			passDesc.userData     = &m_passRecordArguments[viewIndex];

			if (m_viewKinds[viewIndex] == EnViewKind::Shadow)
			{
				// 影は毎フレーム描き直すので、前のフレームの深度を残さず必ず Clear する。色は書かない。
				passDesc.name               = "ShadowPass";
				passDesc.depthTarget        = shadowMap;
				passDesc.depthLoadOperation = EnLoadOperation::Clear;

				graph.AddPass(passDesc);
				continue;
			}

			// 最初のシーン View だけ呼び出し側の指定（Clear か Load か）に従い、以降は前の View が描いたものの上に
			// 重ねる。シャドウ View はここを通らないので、スロットを使っていても判定はずれない。
			const EnLoadOperation viewLoadOperation = isFirstScenePass ? loadOperation : EnLoadOperation::Load;
			isFirstScenePass                        = false;

			passDesc.name               = "ScenePass";
			passDesc.colorTarget        = backBuffer;
			passDesc.colorLoadOperation = viewLoadOperation;
			passDesc.clearColor         = clearColor;
			passDesc.depthTarget        = depthBuffer;
			passDesc.depthLoadOperation = viewLoadOperation;

			// 影のあるフレームだけシャドウマップを読む相手として申告する ➡ 読める用途への遷移が 1 本出る。
			if (m_hasShadowView)
			{
				passDesc.readResources[0]  = shadowMap;
				passDesc.readResourceCount = 1;
			}

			graph.AddPass(passDesc);
		}
	}


	uint32_t SceneRenderer::GetLastDrawnItemCount() const
	{
		uint32_t total = 0;
		for (uint32_t viewIndex = 0; viewIndex < m_viewCount; ++viewIndex)
		{
			total += m_drawnItemCounts[viewIndex];
		}

		return total;
	}


	void SceneRenderer::RecordScenePass(void* userData, rhi::CommandList& commandList)
	{
		const auto& arguments = *static_cast<const ScenePassRecordArguments*>(userData);
		arguments.sceneRenderer->RecordView(arguments.viewIndex, *arguments.device, commandList);
	}


	void SceneRenderer::RecordView(uint32_t viewIndex, rhi::GraphicsDevice& device, rhi::CommandList& commandList)
	{
		const Frustum&                    frustum = m_frustums[viewIndex];
		const std::span<const RenderItem> items   = m_submittedItems[viewIndex];

		const bool isShadowView = m_viewKinds[viewIndex] == EnViewKind::Shadow;

		// ヒープ確保が禁じられたジョブの中なので、生き残った分はスタック上の固定長配列へ積む。
		RenderItem visibleItems[MAX_CULLED_ITEM_COUNT];
		uint32_t   visibleItemCount = 0;

		for (const RenderItem& item : items)
		{
			// 受け専用のものは影を作らない。光の箱を無駄に広げないよう、呼び出し側でなくここで外す。
			if (isShadowView && !item.castsShadow)
			{
				continue;
			}

			if (item.bounds.IsValid() && !frustum.Intersects(item.bounds))
			{
				continue;
			}

			if (visibleItemCount >= MAX_CULLED_ITEM_COUNT)
			{
				FANG_LOG_WARNING(
					Renderer,
					"カリングを生き延びても描けるのは {} 個まで。残りを飛ばした",
					MAX_CULLED_ITEM_COUNT
				);
				break;
			}

			visibleItems[visibleItemCount] = item;
			++visibleItemCount;
		}

		m_drawnItemCounts[viewIndex] = visibleItemCount;

		if (isShadowView)
		{
			// 深度だけを書く。テクスチャもシャドウマップも差さない。
			m_meshRenderer->DrawDepth(
				device,
				commandList,
				m_frameConstantBuffers[viewIndex],
				std::span<const RenderItem>(visibleItems, visibleItemCount)
			);
			return;
		}

		m_meshRenderer->Draw(
			device,
			commandList,
			m_frameConstantBuffers[viewIndex],
			m_shadowMap,
			std::span<const RenderItem>(visibleItems, visibleItemCount)
		);
	}
} // namespace fang
