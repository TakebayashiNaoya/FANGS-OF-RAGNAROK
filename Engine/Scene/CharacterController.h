/**
 * @file CharacterController.h
 * @brief 水平移動・向きの追従・置き物への応答。
 * @details 状態を持たない自由関数だけを置く。コンポーネントの中身から呼ばれる想定。
 *          Collision は接触情報を返すだけなので（01 アーキテクチャ 6）、押し出しの判断はここが持つ。
 */
#pragma once

#include "Collision/Narrowphase.h"
#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include <cstdint>
#include <span>


namespace fang
{
	/**
	 * @brief 1 体ぶんの移動の状態。
	 * @details 値を運ぶだけの POD。持ち主は今のところフレームループ。
	 */
	struct CharacterControllerState
	{
		Vector3 position;             /**< 足元のワールド座標。y は接地で決まる。 */
		float   facingRadians = 0.0f; /**< 水平の向き。0 = +X（狼のモデルが向いている向き）。 */
	};

	/**
	 * @brief 押し出しの 1 件。
	 * @details normal は「この体を外へ出す向き」。Contact::normal は 1 つ目から 2 つ目へ押す向きなので、
	 *          自分が 1 つ目の側にいるときは反転したものが入る。
	 */
	struct PenetrationSample
	{
		Vector3 normal;
		float   depth = 0.0f; /**< めり込みの深さ。0 以上。 */
	};

	/** @brief 1 体が同時に受け取る押し出しの上限。角に挟まれても 3 面あれば足りる。 */
	inline constexpr uint32_t MAX_PENETRATION_SAMPLE_COUNT = 8;

	/**
	 * @brief 押し出し切らずに残す重なり（cm）。
	 * @details 表面まで押し出すと接触が消え、次のフレームは触れていない扱いになって壁へ入り直す
	 *          ➡ 押し出しと再突入をくり返す。わずかに重ねたままにすると接触が残り、進入方向を削り続けられる。
	 *          残す量は狼の体長 204 cm に対して見て分かる大きさではない。
	 */
	inline constexpr float PENETRATION_SKIN_CENTIMETERS = 0.5f;

	/**
	 * @brief 接触の集まりから、自分を外へ出す向きと深さを取り出す。
	 * @param contacts   直近の更新が作った接触。
	 * @param userIndex  自分の登録番号。
	 * @param outSamples 書き込み先。
	 * @return 書いた件数。outSamples を使い切ったらそこで打ち切る。
	 */
	[[nodiscard]] uint32_t CollectPenetrations(
		std::span<const Contact>     contacts,
		uint32_t                     userIndex,
		std::span<PenetrationSample> outSamples
	);

	/**
	 * @brief 押し出す量をまとめて 1 本のベクトルにする。
	 * @param samples CollectPenetrations が書いたもの。空なら 0 が返る。
	 * @return 位置に足すべきベクトル。
	 * @details 向きごとに「まだ解消していない深さ」を見て足す ➡ 同じ向きの接触が何件あっても二重に押さない。
	 *          角で 2 面に挟まれると 1 回では片方が解けないので、収束するか 3 周するまでくり返す。
	 *          PENETRATION_SKIN_CENTIMETERS より浅い重なりは残す（接触を消さないため）。
	 */
	[[nodiscard]] Vector3 ResolvePenetration(std::span<const PenetrationSample> samples);

	/**
	 * @brief 進む向きから、壁へ食い込む成分を取り除く。
	 * @param delta   このフレームに進みたい量。
	 * @param samples 触れている壁。normal はこの体を外へ出す向き。
	 * @return 壁へ入らない向きに削ったもの。2 面に挟まれていれば 0 に近づく。
	 * @details 押し出しと対にして使う。押し出すだけだと、次のフレームで同じだけ食い込んで振動する。
	 */
	[[nodiscard]] Vector3 SlideAlongNormals(const Vector3& delta, std::span<const PenetrationSample> samples);

	/** @brief 接触を解いた後の位置と、実際に進めた量。 */
	struct ContactMoveResult
	{
		Vector3 position;     /**< 押し出しと移動を反映した後。 */
		Vector3 appliedDelta; /**< 壁へ食い込む成分を削った後の、実際に進んだ量。 */
	};

	/**
	 * @brief 前フレームの接触から押し出しつつ、進みたい量を壁に沿わせて足す。
	 * @details CollectPenetrations ➡ ResolvePenetration ➡ SlideAlongNormals を 1 本にまとめたもの。
	 *          狼と雑魚がこれを共有する ➡ 「同じ仕組みで動く」がコードの形になる。
	 */
	[[nodiscard]] ContactMoveResult MoveWithContacts(
		const Vector3&           position,
		const Vector3&           desiredDelta,
		std::span<const Contact> contacts,
		uint32_t                 userIndex
	);

	/**
	 * @brief スティックとカメラの方位から、水平の移動量を作る。
	 * @param stick             長さ 0 〜 1。y が画面の奥、x が画面の右。
	 * @param cameraYawRadians  カメラの方位。狼と同じ規約（0 = +X）。
	 * @param speed             倒し切ったときの速さ（cm / 秒）。
	 * @param deltaTimeSeconds  前のフレームからの実時間。
	 * @return ワールドの水平ベクトル。y は常に 0。
	 */
	[[nodiscard]] Vector3 MakeMoveDelta(
		const Vector2& stick,
		float          cameraYawRadians,
		float          speed,
		float          deltaTimeSeconds
	);

	/**
	 * @brief 今の向きを目標へ最大 maxStepRadians だけ近づける。
	 * @return 詰めた後の向き。-π 〜 π に収まる。
	 * @details 差を -π 〜 π へ畳んでから詰めるので、±π を跨ぐときに遠回りしない。
	 */
	[[nodiscard]] float TurnTowards(float currentRadians, float targetRadians, float maxStepRadians);

	/**
	 * @brief 水平ベクトルの向きを角度にする。
	 * @return 0 = +X、+π/2 = +Z。長さが 0 に近ければ 0。
	 */
	[[nodiscard]] float GetYawFromDirection(const Vector3& direction);
} // namespace fang
