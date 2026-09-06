/**
 * @file MeleeSwing.h
 * @brief 近接攻撃の振り 1 本ぶんの時間割と、判定区間の掃引。
 * @details 状態を持たない自由関数と POD だけを置く。Scene クラスは include しない
 *          （CharacterMovement と同じ性格のもの）。
 */
#pragma once

#include "Collision/CollisionQuery.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"
#include <cstdint>
#include <span>


namespace fang
{
	class CollisionWorld;

	/** @brief 振りの進み具合。 */
	enum class EnMeleeSwingPhase : uint8_t
	{
		Ready,    /**< 待機。次の合図を待つ。 */
		WindUp,   /**< 構え。判定は出ない。 */
		Active,   /**< 判定が出ている区間。1 フレーム 1 掃引。 */
		Recovery, /**< 戻り。判定は出ない。 */
		Cooldown, /**< 次までの待ち。合図が出ていても始まらない。 */
	};

	/** @brief 振りが始まる合図の見かた。 */
	enum class EnMeleeSwingTrigger : uint8_t
	{
		RisingEdge, /**< 合図の立ち上がりだけ。押しっぱなしでは 2 回目が始まらない（パッド向け）。 */
		Continuous, /**< 合図が出ていれば始まる。連射を止めるのは cooldownSeconds（入力を持たない相手向け）。 */
	};

	/** @brief 1 振りで当てられる相手の上限。掃引の書き込み先と同じ数にして、捨てる件数の出どころを 1 つにする。 */
	inline constexpr uint32_t MAX_MELEE_SWING_HIT_COUNT = 8;

	/** @brief 振り 1 種類ぶんの調整値。 */
	struct MeleeSwingParams
	{
		FANG_REFLECT_BEGIN(MeleeSwingParams)
		FANG_FIELD(windUpSeconds, "構えの秒数", Range(0.0f, 5.0f))
		FANG_FIELD(activeSeconds, "判定区間の秒数", Range(0.0f, 5.0f))
		FANG_FIELD(recoverySeconds, "戻りの秒数", Range(0.0f, 5.0f))
		FANG_FIELD(cooldownSeconds, "次までの待ち", Range(0.0f, 10.0f))
		FANG_FIELD(reachCentimeters, "間合い", Range(0.0f, 1000.0f))
		FANG_FIELD(fangHeightCentimeters, "牙の高さ", Range(0.0f, 500.0f))
		FANG_FIELD(fangRadiusCentimeters, "牙の太さ", Range(0.0f, 200.0f))
		FANG_FIELD(arcRadians, "薙ぐ弧の中心角", Range(0.0f, PI))
		FANG_FIELD(attackPower, "攻撃力", Range(0.0f, 10000.0f))
		FANG_REFLECT_END()

		float windUpSeconds   = 0.10f;
		float activeSeconds   = 0.15f;
		float recoverySeconds = 0.20f;

		float cooldownSeconds = 0.0f; /**< 戻りのあと、次の振りを受け付けるまで。 */

		float reachCentimeters      = 150.0f; /**< 体の中心から牙まで。体長 204cm より短い ➡ 壁越しに届かない。 */
		float fangHeightCentimeters = 100.0f;
		float fangRadiusCentimeters = 40.0f;

		float arcRadians  = 2.0f;  /**< 判定区間で牙が描く弧。左から右へ薙ぐ。 */
		float attackPower = 50.0f; /**< 1 回の当たりで減らす HP。 */

		/** @brief 合図の見かた。調整つまみではないので FANG_FIELD には出さない。 */
		EnMeleeSwingTrigger triggerMode = EnMeleeSwingTrigger::RisingEdge;
	};

	/** @brief 振り 1 本ぶんの状態。持ち主は呼び出し側。 */
	struct MeleeSwingState
	{
		EnMeleeSwingPhase phase              = EnMeleeSwingPhase::Ready;
		bool              wasAttackRequested = false; /**< 立ち上がりを見るための前フレームの値。 */

		float elapsedSeconds = 0.0f; /**< 今の位相に入ってからの秒数。 */

		/** @brief 直前に掃引した牙の位置。判定区間に入った瞬間に弧の始点で埋める。 */
		Vector3 previousFangPosition;

		/** @brief この振りで既に当てた席番号。振りの開始で捨てる。 */
		uint32_t hitUserIndices[MAX_MELEE_SWING_HIT_COUNT] = {};
		uint8_t  hitCount                                  = 0;
	};

	/** @brief 振りを 1 フレーム進めるための、その瞬間の値。 */
	struct MeleeSwingInput
	{
		Vector3 selfPosition;             /**< 足元のワールド座標。 */
		float   selfFacingRadians = 0.0f; /**< 0 = +X。 */

		bool isAttackRequested = false;

		uint32_t selfUserIndex       = 0;
		uint32_t targetAttributeMask = ALL_COLLISION_ATTRIBUTE_MASK; /**< 攻撃が当たる種別。意味は Game が決める。 */
	};

	/** @brief 1 フレームの答え。 */
	struct MeleeSwingResult
	{
		/** @brief outHits の先頭から詰めた、この振りで初めて当たった相手の数。 */
		uint32_t newHitCount = 0;

		bool didStartSwing = false; /**< このフレームに振りが始まった。 */
		bool didSweep      = false; /**< 掃引を投げた。1 フレームの本数を数えるため。 */

		/** @brief 掃引か記録の書き込み先が足りず、遠いほうを捨てた。 */
		bool isTruncated = false;
	};

	/** @brief 振りが進んでいる最中か。構え・判定・戻りの間は true。待機と次までの待ちは false。 */
	[[nodiscard]] inline bool IsMeleeSwingInProgress(const MeleeSwingState& state)
	{
		return state.phase == EnMeleeSwingPhase::WindUp || state.phase == EnMeleeSwingPhase::Active ||
			   state.phase == EnMeleeSwingPhase::Recovery;
	}

	/**
	 * @brief 振りを 1 フレーム進め、この振りで初めて当たった相手を近い順に書き出す。
	 * @param outHits 書き込み先。掃引の作業場も兼ねる ➡ 戻った時点で先頭 newHitCount 件だけが有効。
	 * @details 掃引を投げるのは判定区間のフレームだけ。牙の位置は位置と向きから幾何的に出すので、
	 *          スキニング行列（振る舞いの後で作る）より 1 フレーム分だけ新しい。
	 *          見るのは前フレームの登録（ADR-034）。
	 * @threading 更新ジョブ 1 本から。world は const で読むだけ。
	 */
	[[nodiscard]] MeleeSwingResult StepMeleeSwing(
		const CollisionWorld&   world,
		const MeleeSwingParams& params,
		const MeleeSwingInput&  input,
		float                   deltaTimeSeconds,
		MeleeSwingState*        state,
		std::span<SweepHit>     outHits
	);

	/**
	 * @brief 判定区間の進み具合から牙の位置を出す。
	 * @param activeRatio 判定区間の始めを 0、終わりを 1 とした進み具合。
	 */
	[[nodiscard]] Vector3 ComputeFangPosition(
		const MeleeSwingParams& params,
		const Vector3&          selfPosition,
		float                   selfFacingRadians,
		float                   activeRatio
	);
} // namespace fang
