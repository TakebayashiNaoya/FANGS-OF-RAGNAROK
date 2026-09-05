/**
 * @file MinionBehavior.h
 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
 */
#pragma once

#include "AI/AI.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Scene/Scene.h"
#include <span>


namespace fang
{
	class CollisionWorld;
	class HeightmapTerrain;
} // namespace fang


namespace fang::game
{
	/** @brief 雑魚が共有する感知・追跡の調整値。複数体で共有するので、Dependencies はポインタで参照する。 */
	struct MinionParams
	{
		PerceptionParams perception;
		PursuitParams    pursuit;

		/** @brief 湧いたときの HP。攻撃力 50 に対して 100 ➡ 2 回で倒れる。 */
		float maximumHitPoints = 100.0f;
	};

	/**
	 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
	 * @details 攻撃も HP も持たない。パッドを触らず、センサー ➡ ブラックボード ➡ 意思決定 ➡ エフェクターの
	 *          順で 1 フレームを進める。見た目は狼と共有の歩行ポーズをそのまま指す
	 *          （重いデータは Game 側の WolfModel が持つ）。
	 */
	class MinionBehavior final : public IComponent
	{
	public:
		/** @brief 共有する調整値と、Game 側が持ち続ける資源への借用。 */
		struct Dependencies
		{
			/** @brief 感知・追跡の調整値。複数体で共有するので値で持たない。 */
			const MinionParams* params = nullptr;

			/** @brief 当たり判定の入れ物。作れなかったときだけ nullptr（押し出しと感知を飛ばす）。 */
			CollisionWorld* collisionWorld = nullptr;

			/** @brief 接地の高さの問い合わせ先。読めていなければ nullptr（y = 0 に立つ）。 */
			const HeightmapTerrain* terrain = nullptr;

			/** @brief 追いかける相手（操作している狼）。 */
			GameObjectHandle targetHandle;

			/** @brief 狼と共有する歩行ポーズ。空なら SetSkinningMatrices を呼ばない。 */
			std::span<const Matrix4x4> skinningMatricesStorage;
		};

		MinionBehavior(const Dependencies& dependencies, const Vector3& initialPosition);

		void Update(float deltaTimeSeconds, GameObjectHandle self, Scene& scene) override;


	private:
		Dependencies m_dependencies;

		Vector3 m_position; /**< 足元のワールド座標。y は常に 0（接地は Update の中で足す）。 */
		float   m_facingRadians = 0.0f;

		AgentBlackboard m_blackboard;
		EnPursuitState  m_state = EnPursuitState::Idle;
	};
} // namespace fang::game
