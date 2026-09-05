/**
 * @file CollisionShapes.h
 * @brief 当たり判定の形（球・カプセル・OBB）と、描画用の箱からの導出。
 */
#pragma once

#include "Core/Math/Aabb.h"
#include "Core/Math/Vector3.h"
#include <cstdint>


namespace fang
{
	struct Matrix4x4;

	/** @brief ColliderShape が持っている形の種類。 */
	enum class EnShapeType : uint8_t
	{
		Sphere,
		Capsule,
		OBB,
	};

	/** @brief 球。 */
	struct Sphere
	{
		Vector3 center;
		float   radius = 0.0f;
	};

	/**
	 * @brief カプセル。中心線の線分と、その周りの半径で表す。
	 * @details pointA と pointB が重なっていれば球と同じ形になる ➡ 線分が潰れても別の型に切り替えずに済む。
	 */
	struct Capsule
	{
		Vector3 pointA; /**< 中心線の端。 */
		Vector3 pointB; /**< 中心線のもう一方の端。 */
		float   radius = 0.0f;
	};

	/**
	 * @brief 軸に沿わない箱。
	 * @details 姿勢は正規化した直交軸 3 本で持つ。Core が四元数を持っていないのと、判定で使うのが結局は
	 *          軸そのものだから ➡ 判定のたびに四元数から軸へ直さずに済む。
	 */
	struct OBB
	{
		Vector3 center;
		Vector3 axes[3] = {
			{ 1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f },
		}; /**< 正規化した直交する 3 軸。 */
		Vector3 halfExtents; /**< 各軸方向の半分の長さ。 */
	};

	/**
	 * @brief 3 つの形のどれか 1 つ。
	 * @details type が読んでよいメンバを決める。3 つとも自明に壊せる型なので、POD のまま固定長の配列に置ける。
	 *          共用体のまま書くと既定の構築ができないので、作るときは MakeColliderShape を通す。
	 */
	struct ColliderShape
	{
		EnShapeType type = EnShapeType::Sphere;

		union
		{
			Sphere  sphere{};
			Capsule capsule;
			OBB     obb;
		};
	};

	/** @brief 球を ColliderShape に包む。 */
	[[nodiscard]] ColliderShape MakeColliderShape(const Sphere& sphere);

	/** @brief カプセルを ColliderShape に包む。 */
	[[nodiscard]] ColliderShape MakeColliderShape(const Capsule& capsule);

	/** @brief OBB を ColliderShape に包む。 */
	[[nodiscard]] ColliderShape MakeColliderShape(const OBB& box);

	/**
	 * @brief 形を包むワールド空間の箱を求める。
	 * @param shape 包む形。
	 * @return 形をすべて含む軸平行の箱。Broadphase の入力になる。
	 */
	[[nodiscard]] Aabb ComputeBounds(const ColliderShape& shape);

	/**
	 * @brief モデル空間の箱と world 行列から OBB を作る。
	 * @param localBounds モデル空間の箱。有効であること（IsValid）。
	 * @param world       行ベクトル規約の変換行列（p * M）。
	 * @return 箱をそのまま回して移した OBB。行に拡大率が入っていれば halfExtents に掛かる。
	 */
	[[nodiscard]] OBB MakeOBBFromAabb(const Aabb& localBounds, const Matrix4x4& world);

	/**
	 * @brief モデル空間の箱と world 行列からカプセルを作る。
	 * @param localBounds モデル空間の箱。有効であること（IsValid）。
	 * @param world       行ベクトル規約の変換行列（p * M）。
	 * @return 元の箱に必ず収まるカプセル。
	 * @details 中心線は箱のいちばん長い軸に沿わせ、半径は残り 2 軸の半分の小さいほうにする。線分の端は
	 *          半径ぶん内側へ詰める ➡ 端の半球が箱からはみ出さない。
	 */
	[[nodiscard]] Capsule MakeCapsuleFromAabb(const Aabb& localBounds, const Matrix4x4& world);
} // namespace fang
