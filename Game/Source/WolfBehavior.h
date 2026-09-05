/**
 * @file WolfBehavior.h
 * @brief 狼 1 体ぶんの移動・接地・アニメーションを進める振る舞い。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Input/Gamepad.h"
#include "Scene/Scene.h"
#include "WolfMovementParams.h"
#include <span>


namespace fang
{
	class CollisionWorld;
	class HeightmapTerrain;
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
	 */
	class WolfBehavior final : public IComponent
	{
	public:
		/** @brief WolfModel など、Game 側が持ち続ける資源への借用。 */
		struct Dependencies
		{
			/** @brief 当たり判定の入れ物。作れなかったときだけ nullptr（押し出しを飛ばす）。 */
			CollisionWorld* collisionWorld = nullptr;

			/** @brief 接地の高さの問い合わせ先。読めていなければ nullptr（y = 0 に立つ）。 */
			const HeightmapTerrain* terrain = nullptr;

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
		 * @param isControlled       true ならパッドで動かす。false なら初期位置に立ったまま、
		 *                           共有スキニング行列だけを毎フレーム書き直す。
		 * @param initialPosition    足元のワールド座標。y は接地で決まるので 0 でよい。
		 * @param initialFacingRadians 初期の向き。0 = +X。
		 */
		WolfBehavior(
			bool                      isControlled,
			const WolfMovementParams& params,
			const Dependencies&       dependencies,
			const Vector3&            initialPosition,
			float                     initialFacingRadians
		);

		void Update(float deltaTimeSeconds, GameObjectHandle self, Scene& scene) override;

		/**
		 * @brief 周の頭でメインスレッドが読んだパッドと、カメラの水平回転角を渡す。
		 * @details ReadGamepadState はメインスレッドのみなので、更新ジョブの中からは呼べない。Game::OnUpdate が
		 *          Scene::Update より前にこれを呼んで橋渡しする。isControlled が false なら呼ばなくてよい。
		 */
		void SetFrameInput(const GamepadState& gamepad, float cameraYawRadians);

		/** @brief 直近の Update が計算した足元のワールド座標（接地前、y は常に 0）。 */
		[[nodiscard]] Vector3 GetPosition() const { return m_position; }

		/** @brief 直近の Update が計算した向き。0 = +X。 */
		[[nodiscard]] float GetFacingRadians() const { return m_facingRadians; }


	private:
		bool               m_isControlled;
		WolfMovementParams m_params;
		Dependencies       m_dependencies;

		Vector3 m_position; /**< 足元のワールド座標。y は常に 0（接地は Update の中で足す）。 */
		float   m_facingRadians = 0.0f;

		GamepadState m_gamepad;
		float        m_cameraYawRadians = 0.0f;
	};
} // namespace fang::game
