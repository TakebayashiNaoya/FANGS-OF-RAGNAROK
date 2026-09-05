/**
 * @file Narrowphase.h
 * @brief 形の組ごとの厳密な接触判定。
 */
#pragma once

#include "Collision/CollisionShapes.h"
#include "Core/Math/Vector3.h"
#include <cstdint>


namespace fang
{
	/**
	 * @brief 2 つの形が触れている状態。
	 * @details 返すのは判定の結果だけで、押し戻しやダメージは呼び出し側の仕事。
	 */
	struct Contact
	{
		/**
		 * @brief 1 つ目の形の呼び出し側の番号。
		 * @details 埋めるのは CollisionWorld。Intersect を直に呼んだときは触られない。
		 */
		uint32_t userIndexA = 0;
		uint32_t userIndexB = 0; /**< 2 つ目の形の呼び出し側の番号。 */

		Vector3 point;  /**< ワールド空間の接触点。2 つの表面の中間。 */
		Vector3 normal; /**< 1 つ目の形から 2 つ目の形へ押し出す向き。長さ 1。 */

		float depth = 0.0f; /**< めり込みの深さ。0 以上。 */
	};

	/**
	 * @brief 球どうし。
	 * @param outContact 触れているときだけ書く。null は不可。
	 * @return 触れていれば true。
	 * @details 中心が重なるなど向きを決められないときは +Y へ押し出す ➡ 長さ 0 のベクトルを正規化しない。
	 */
	[[nodiscard]] bool Intersect(const Sphere& a, const Sphere& b, Contact* outContact);

	/** @brief 球とカプセル。中心と中心線の最近点で判定する。 */
	[[nodiscard]] bool Intersect(const Sphere& a, const Capsule& b, Contact* outContact);

	/**
	 * @brief 球と OBB。
	 * @details 中心を OBB のローカルへ移して軸ごとに clamp する。中心が箱の中に入っていたら、いちばん近い
	 *          面へ抜く向きを法線にして、その面までの距離を深さへ足す。
	 */
	[[nodiscard]] bool Intersect(const Sphere& a, const OBB& b, Contact* outContact);

	/** @brief カプセルどうし。中心線どうしの最近点で判定する。 */
	[[nodiscard]] bool Intersect(const Capsule& a, const Capsule& b, Contact* outContact);

	/**
	 * @brief カプセルと OBB。
	 * @details 線分を OBB のローカルへ移し、「箱へ clamp ➡ 線分へ投影し直す」を 4 回くり返して中心線上の
	 *          最近点を決め、そこから先は球と OBB と同じ経路を通る。反復回数が定数なので最悪の時間が決まり、
	 *          入力が同じなら必ず同じ答えになる。
	 */
	[[nodiscard]] bool Intersect(const Capsule& a, const OBB& b, Contact* outContact);

	/**
	 * @brief OBB どうし。
	 * @details 分離軸判定（面 3 + 3、辺の外積 9 の 15 軸）。重なりがいちばん浅い軸を法線にする。接触点は
	 *          その向きの支持点 2 つの中点で、1 点の近似。押し戻しに要る法線と深さは正確に出る。
	 */
	[[nodiscard]] bool Intersect(const OBB& a, const OBB& b, Contact* outContact);

	/**
	 * @brief 種類で振り分けて上の 6 本のどれかへ渡す。
	 * @details 異種の組は順を入れ替えて呼び、法線を反転して 1 つ目 ➡ 2 つ目の向きへ戻す。
	 */
	[[nodiscard]] bool Intersect(const ColliderShape& a, const ColliderShape& b, Contact* outContact);
} // namespace fang
