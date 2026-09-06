/**
 * @file WolfController.h
 * @brief 狼 1 体ぶんの移動・接地・アニメーションを進める振る舞い。
 */
#pragma once

#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Input/Gamepad.h"
#include "Scene/CharacterBase.h"
#include "Scene/MeleeSwing.h"
#include "Scene/Scene.h"
#include "WolfMovementParameter.h"
#include <span>


namespace fang
{
	class SkeletalAnimation;
	class AnimationPlayback;
	struct Matrix4x4;
} // namespace fang


namespace fang::game
{
	/**
	 * @brief 狼の移動・接地・アニメーションを進める振る舞い。
	 * @details 操作する 1 体（isControlled）だけがパッドと当たり判定を見て動く。残りは初期位置に置いたまま、
	 *          スキニング行列だけを操作する狼と共有する（2 体が同じ歩行ポーズで動く）。
	 *          重いデータ（SkeletalAnimation・逆バインド行列・スキニング行列の置き場）は Game 側の WolfModel が
	 *          持ち、ここではポインタと span だけを借りる ➡ Scene::BEHAVIOR_BLOCK_SIZE に収まる。
	 *          位置・向き・当たり判定・地形は CharacterBase（基底）の持ち物。
	 */
	class WolfController final : public CharacterBase
	{
	public:
		/** @brief WolfModel など、Game 側が持ち続ける資源への借用。 */
		struct Dependencies
		{
			/** @brief 移動の調整値。狼 2 体が同じ実体を読む ➡ つまみが 1 回で両方に効く。 */
			const WolfMovementParameter* parameter = nullptr;

			/** @brief 牙の時間割・間合い・攻撃力。同上。 */
			const MeleeSwingParameter* swingParameter = nullptr;

			/** @brief 骨を持つメッシュとして読めたか。false なら以下は使わない。 */
			bool isSkinned = false;

			/** @brief 読み込みに成功していれば非 null。 */
			SkeletalAnimation* animation = nullptr;

			/** @brief 再生位置。操作する狼だけが進める。 */
			AnimationPlayback* playback = nullptr;

			std::span<const Matrix4x4> inverseBindMatrices;

			/**
			 * @brief 毎フレーム書き直すスキニング行列の置き場。
			 * @details WolfModel が読み込み時に確保した固定の置き場。ここへ書いた span をそのまま
			 *          Scene::SetSkinningMatrices へ渡す ➡ 複数の狼が同じ置き場を指せば姿勢を共有できる。
			 */
			std::span<Matrix4x4> skinningMatricesStorage;
		};

		/**
		 * @param dependencies       移動・牙の調整値と WolfModel など、Game 側が持ち続ける資源への借用。
		 * @param collisionWorld     当たり判定の入れ物。CharacterBase へそのまま渡す。nullptr なら押し戻さない。
		 * @param terrain            接地の高さの問い合わせ先。CharacterBase へそのまま渡す。nullptr なら y = 0。
		 * @param initialPosition    足元のワールド座標。y は接地で決まるので 0 でよい。
		 * @param initialFacingRadians 初期の向き。0 = +X。
		 * @details 生成した時点では操作対象ではない（初期位置に立ったまま、共有スキニング行列だけを毎フレーム
		 *          書き直す）。操作対象にするかどうかは WolfManager が SetControlled で決める。
		 */
		WolfController(
			const Dependencies&     dependencies,
			CollisionWorld*         collisionWorld,
			const HeightmapTerrain* terrain,
			const Vector3&          initialPosition,
			float                   initialFacingRadians
		);

		void Update(float deltaTimeSeconds, Actor self) override;

		/**
		 * @brief 操作対象にする / 外す。
		 * @details 引き継いだ直後に、押しっぱなしのボタンで振り出さないようにする（押されている扱いから始める）
		 *          ➡ 一度離して押し直すまで振らない。
		 */
		void SetControlled(bool isControlled);

		/**
		 * @brief 周の頭でメインスレッドが読んだパッドと、カメラの水平回転角を渡す。
		 * @details ReadGamepadState はメインスレッドのみなので、更新ジョブの中からは呼べない。Game::OnUpdate が
		 *          Scene::Update より前にこれを呼んで橋渡しする。操作対象でなければ呼ばなくてよい。
		 *          受け取った GamepadState のうち使う値（左スティック・X ボタン）だけを取り出して持つ。
		 */
		void SetFrameInput(const GamepadState& gamepad, float cameraYawRadians);


	private:
		bool         m_isControlled = false;
		Dependencies m_dependencies;

		Vector2 m_moveStick; /**< GetLeftStick を通した後。 */
		float   m_cameraYawRadians  = 0.0f;
		bool    m_isAttackRequested = false; /**< X ボタン。 */

		/** @brief 攻撃ボタン（X）の振り 1 本ぶんの状態。操作対象でなければ進まない。 */
		MeleeSwingState m_swingState;
	};

	static_assert(
		sizeof(WolfController) + Scene::BEHAVIOR_HEADROOM_SIZE <= Scene::BEHAVIOR_BLOCK_SIZE,
		"WolfController がブロックの余白を食い潰した。BEHAVIOR_BLOCK_SIZE の引き上げとセットで考えること"
	);

	// 移動・牙の調整値をポインタ 2 本で借りるようにした（ADR-046 が計った 224 バイトから、値で持っていた
	// WolfMovementParameter + MeleeSwingParameter の 52 バイトが抜けた）。縮んでいなければ複製が戻っている。
	static_assert(sizeof(WolfController) < 224, "WolfController が値の複製を持たない形に縮んでいない");
} // namespace fang::game
