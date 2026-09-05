/**
 * @file CollisionDebugLines.h
 * @brief コライダーのワイヤーフレームを線分の列にする。
 */
#pragma once

#include "Collision/CollisionShapes.h"
#include "Core/Math/Vector3.h"
#include <cstdint>
#include <span>


namespace fang
{
	/** @brief ワールド空間の線分 1 本。色は付けない（呼び出し側が接触の有無などで決める）。 */
	struct DebugLineSegment
	{
		Vector3 from;
		Vector3 to;
	};

	/** @brief 円 1 周の分割数。 */
	inline constexpr uint32_t CIRCLE_SEGMENT_COUNT = 12;

	/** @brief 半円の分割数。 */
	inline constexpr uint32_t HALF_CIRCLE_SEGMENT_COUNT = CIRCLE_SEGMENT_COUNT / 2;

	/** @brief 球に要る本数。直交する 3 つの円。 */
	inline constexpr uint32_t SPHERE_LINE_COUNT = CIRCLE_SEGMENT_COUNT * 3;

	/** @brief カプセルに要る本数。両端の円 2 つ + 端の半円 4 つ + 側面 4 本。 */
	inline constexpr uint32_t CAPSULE_LINE_COUNT = CIRCLE_SEGMENT_COUNT * 2 + HALF_CIRCLE_SEGMENT_COUNT * 4 + 4;

	/** @brief OBB に要る本数。箱の辺。 */
	inline constexpr uint32_t BOX_LINE_COUNT = 12;

	/** @brief どの形でも足りる本数。作業用の配列の大きさに使う。 */
	inline constexpr uint32_t MAX_SHAPE_LINE_COUNT = CAPSULE_LINE_COUNT;

	/**
	 * @brief 形を描くのに要る線分の本数。
	 * @param shape 描く形。
	 * @return 本数。形の大きさによらず種類だけで決まる。
	 */
	[[nodiscard]] uint32_t GetShapeLineCount(const ColliderShape& shape);

	/**
	 * @brief 形のワイヤーフレームを線分の列にする。
	 * @param shape       描く形。退化していても本数は変わらない（長さ 0 の線分になる）。
	 * @param outSegments 書き込み先。
	 * @return 書いた本数。GetShapeLineCount より短い span を渡すと、そこで打ち切る。
	 * @details CollisionWorld のメソッドではなく自由関数にしてある ➡ 登録していない形も描けるし、
	 *          状態を持たないのでジョブからも呼べる。描くのは呼び出し側（Renderer を Collision が知らない）。
	 */
	[[nodiscard]] uint32_t BuildShapeLines(const ColliderShape& shape, std::span<DebugLineSegment> outSegments);
} // namespace fang
