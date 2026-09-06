/**
 * @file EnemyController.h
 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
 */
#pragma once

#include "AI/AI.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"
#include "Scene/CharacterBase.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include "CollisionAttribute.h"
#include <span>


namespace fang::game
{
	/** @brief 雑魚が狼へ詰める距離。牙の間合いの内側に入るまで詰める ➡ 止まった位置から届く。 */
	inline constexpr float ENEMY_STOP_DISTANCE_CENTIMETERS = 120.0f;

	/** @brief 雑魚の牙の間合い。狼と同じ体なので同じ値。 */
	inline constexpr float ENEMY_REACH_CENTIMETERS = 150.0f;

	static_assert(
		ENEMY_STOP_DISTANCE_CENTIMETERS < ENEMY_REACH_CENTIMETERS,
		"雑魚が止まる位置から牙が届かない（停止距離が間合い以上）"
	);

	/** @brief 雑魚が共有する感知・追跡・振りの調整値。複数体で共有するので、Dependencies はポインタで参照する。 */
	struct EnemyParameter
	{
		FANG_REFLECT_BEGIN(EnemyParameter)
		FANG_FIELD_NESTED(perception, "感知")
		FANG_FIELD_NESTED(pursuit, "追跡")
		FANG_FIELD_NESTED(swing, "牙")
		FANG_FIELD(maximumHitPoints, "最大 HP", Range(1.0f, 10000.0f))
		FANG_REFLECT_END()

		PerceptionParameter perception{ .blockerAttributeMask = COLLISION_ATTRIBUTE_PROP };
		PursuitParameter    pursuit{ .stopDistanceCentimeters = ENEMY_STOP_DISTANCE_CENTIMETERS };

		/** @brief 振りの時間割。1 周 1.00 秒（0.30 + 0.15 + 0.25 + 0.30）。 */
		MeleeSwingParameter swing{
			.windUpSeconds    = 0.30f,
			.activeSeconds    = 0.15f,
			.recoverySeconds  = 0.25f,
			.cooldownSeconds  = 0.30f,
			.reachCentimeters = ENEMY_REACH_CENTIMETERS,
			.attackPower      = 25.0f,
			.triggerMode      = EnMeleeSwingTrigger::Continuous,
		};

		/** @brief 湧いたときの HP。攻撃力 25 に対して 100 ➡ 2 回で倒れる。 */
		float maximumHitPoints = 100.0f;
	};

	/**
	 * @brief 雑魚 1 体ぶんの感知・追跡・移動・接地を進める振る舞い。
	 * @details 攻撃も HP も持たない。パッドを触らず、センサー ➡ ブラックボード ➡ 意思決定 ➡ エフェクターの
	 *          順で 1 フレームを進める。見た目は狼と共有の歩行ポーズをそのまま指す
	 *          （重いデータは Game 側の WolfModel が持つ）。位置・向き・当たり判定・地形は
	 *          CharacterBase（基底）の持ち物。
	 */
	class EnemyController final : public CharacterBase
	{
	public:
		/** @brief 共有する調整値と、Game 側が持ち続ける資源への借用。 */
		struct Dependencies
		{
			/** @brief 感知・追跡の調整値。複数体で共有するので値で持たない。 */
			const EnemyParameter* parameter = nullptr;

			/** @brief 追いかける相手（今の操作対象）。Game が 1 か所で持ち替えるので、値では持たない。 */
			const Actor* target = nullptr;

			/** @brief 狼と共有する歩行ポーズ。空なら SetSkinningMatrices を呼ばない。 */
			std::span<const Matrix4x4> skinningMatricesStorage;
		};

		EnemyController(
			const Dependencies&     dependencies,
			CollisionWorld*         collisionWorld,
			const HeightmapTerrain* terrain,
			const Vector3&          initialPosition
		);

		void Update(float deltaTimeSeconds, Actor self) override;


	private:
		Dependencies m_dependencies;

		AgentBlackboard m_blackboard;
		EnPursuitState  m_state = EnPursuitState::Idle;

		/** @brief 牙の振り 1 本ぶんの状態。 */
		MeleeSwingState m_swingState;
	};

	static_assert(
		sizeof(EnemyController) + Scene::BEHAVIOR_HEADROOM_SIZE <= Scene::BEHAVIOR_BLOCK_SIZE,
		"EnemyController がブロックの余白を食い潰した。BEHAVIOR_BLOCK_SIZE の引き上げとセットで考えること"
	);
} // namespace fang::game
