/**
 * @file Stage.h
 * @brief ステージの置き物の読み込みと、Scene オブジェクトとしての一括生成。
 */
#pragma once

#include "RHI/RHIHandles.h"
#include "Renderer/MeshRenderer.h"
#include "Scene/Scene.h"
#include <vector>


namespace fang
{
	class HeightmapTerrain;
} // namespace fang


namespace fang::rhi
{
	class GraphicsDevice;
} // namespace fang::rhi


namespace fang::game
{
	/**
	 * @brief ステージの GPU リソースの器。Game が読み込みから解放まで持つ。
	 * @details Scene の MeshRendererComponent は席番号とローカル境界ボックスだけを持つので、GPU リソースの
	 *          寿命はこちらが握る（終了処理でここから DestroyTexture する。メッシュは MeshRenderer::Shutdown
	 *          がまとめて解放するので持たない）。
	 */
	struct StageModel
	{
		std::vector<rhi::TextureHandle> textures;

		/**
		 * @brief 専用アセットが無い物の見た目に貸す静的メッシュ(MarkerPyramid)。
		 * @details ステージが読めない・名前のメッシュが無い・GPU へ載らない、のどれでも無効のまま。
		 *          借りる側は無効なら見た目無しで動く(ADR-062)。寿命は MeshRenderer::Shutdown が持つ。
		 */
		MeshId placeholderMesh;
		Aabb   placeholderLocalBounds;
	};

	/**
	 * @brief ステージを読み、メッシュを GPU へ載せ、配置ごとに Scene オブジェクトを 1 つずつ作る。
	 * @details 失敗しても落とさない。読めなければ置き物なしで続く（ログだけ出す）。Scene の上限に
	 *          当たったら、そこで打ち切る（Scene::CreateObject が警告を出す）。
	 * @param terrain 接地に使う地形。読めていなければ nullptr を渡す ➡ 配置は glTF のまま y = 0 に置く。
	 */
	void LoadAndCreateStageObjects(
		rhi::GraphicsDevice&    device,
		MeshRenderer&           meshRenderer,
		Scene&                  scene,
		const HeightmapTerrain* terrain,
		StageModel*             outStageModel
	);
} // namespace fang::game
