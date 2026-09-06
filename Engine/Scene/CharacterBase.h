/**
 * @file CharacterBase.h
 * @brief 地表に立ち、壁に押し戻される 1 体ぶんの土台。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Scene/Actor.h"
#include "Scene/CharacterController.h"
#include "Scene/ComponentTypes.h"
#include <cstddef>


namespace fang
{
	class CollisionWorld;
	class HeightmapTerrain;

	/**
	 * @brief 地表に立ち、壁に押し戻される 1 体ぶんの土台。
	 * @details 位置・向き・当たり判定・地形だけを持つ。振り・入力・AI・見た目は派生の持ち物。
	 *          Update は実装しない（派生が必ず書く）。
	 * @threading 更新ジョブ 1 本から。
	 */
	class CharacterBase : public IComponent
	{
	public:
		/** @brief 足元のワールド座標。y は接地の前（常に 0）。 */
		[[nodiscard]] Vector3 GetPosition() const { return m_state.position; }

		/** @brief 水平の向き。0 = +X。 */
		[[nodiscard]] float GetFacingRadians() const { return m_state.facingRadians; }


	protected:
		/** @brief Game 側が持ち続ける資源への借用。どちらも nullptr でよい。 */
		struct GroundDependencies
		{
			/** @brief 当たり判定の入れ物。nullptr なら押し戻さない。 */
			CollisionWorld* collisionWorld = nullptr;

			/** @brief 接地の高さの問い合わせ先。nullptr なら y = 0 に立つ。 */
			const HeightmapTerrain* terrain = nullptr;
		};

		CharacterBase(
			const GroundDependencies& dependencies,
			const Vector3&            initialPosition,
			float                     initialFacingRadians
		);

		/**
		 * @brief 前フレームの接触から押し出しつつ、進みたい量を壁に沿わせて足す。
		 * @return 壁へ食い込む成分を削った後の、実際に進んだ量。
		 * @details 見るのは前フレームの接触（ADR-034）。当たり判定が無ければ押し戻しなしで足すだけ。
		 */
		Vector3 MovePosition(const Vector3& desiredDelta, uint32_t selfUserIndex);

		/** @brief 今の向きを目標へ最大 maxStepRadians だけ近づける。 */
		void TurnFacingTowards(float targetRadians, float maxStepRadians);

		/**
		 * @brief 足元へ地表の高さを足して Scene へ書く。
		 * @details 1 フレームに 1 回だけ呼ぶこと（1 オブジェクトの Transform を書けるのは 1 人、ADR-041）。
		 */
		void WriteTransform(Actor self) const;

		[[nodiscard]] CollisionWorld*         GetCollisionWorld() const { return m_collisionWorld; }
		[[nodiscard]] const HeightmapTerrain* GetTerrain() const { return m_terrain; }


	private:
		CharacterControllerState m_state; /**< 位置と向き。既存の POD をそのまま使う。 */
		CollisionWorld*          m_collisionWorld = nullptr;
		const HeightmapTerrain*  m_terrain        = nullptr;
	};

	/** @brief 基底が食ってよいバイト数。ここを上げるのは BEHAVIOR_BLOCK_SIZE の引き上げとセット。 */
	inline constexpr size_t CHARACTER_BASE_SIZE_LIMIT = 40;

	static_assert(
		sizeof(CharacterBase) <= CHARACTER_BASE_SIZE_LIMIT,
		"CharacterBase が太った。派生 1 つにつき同じだけ余地が減る（設計.md「大きさ」）"
	);
} // namespace fang
