/**
 * @file ItemDrop.h
 * @brief 撃破からアイテムのドロップ・拾い・寿命までの、状態を持たない計算。
 * @details Scene クラスも Actor も include しない。Vector3 と float と整数しか受け取らないので、
 *          テストは Engine だけで回る（LevelGrowth / MeleeSwing / CameraOcclusion と同じ性格）。
 */
#pragma once

#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Core/Reflection/Reflection.h"
#include <cstdint>
#include <span>


namespace fang
{
	/** @brief 落ちる・拾う・使う・消える・見せるの調整値。1 種類しかないので「肉の」とは名乗らない。 */
	struct ItemDropParameter
	{
		FANG_REFLECT_BEGIN(ItemDropParameter)
		FANG_FIELD(dropProbability, "ドロップ率", Range(0.0f, 1.0f))
		FANG_FIELD(pickupRadiusCentimeters, "回収の半径", Range(0.0f, 2000.0f))
		FANG_FIELD(lifetimeSeconds, "落ちた物の寿命", Range(0.0f, 120.0f))
		FANG_FIELD(pickupDelaySeconds, "拾えるまでの待ち", Range(0.0f, 10.0f))
		FANG_FIELD(healRatio, "回復の割合", Range(0.0f, 1.0f))
		FANG_FIELD(bagCapacity, "バッグの上限", Range(0.0f, 99.0f))
		FANG_FIELD(hoverHeightCentimeters, "浮かせる高さ", Range(0.0f, 200.0f))
		FANG_FIELD(rotationsPerSecond, "毎秒の回転数", Range(0.0f, 5.0f))
		FANG_FIELD(displayScale, "見た目の倍率", Range(0.1f, 5.0f))
		FANG_REFLECT_END()

		float dropProbability = 0.1f; /**< 0 で 1 個も落ちず、1 で必ず落ちる。 */

		/** @brief 狼の中心からこの距離に入ったフレームに自動で入る。牙の間合い 150 より広い。 */
		float pickupRadiusCentimeters = 200.0f;

		float lifetimeSeconds = 15.0f;

		/** @brief 落ちてからこの秒数は拾えない。落ちたことが見えてから入る(GameRules 5)。寿命以上なら拾えないまま消える。 */
		float pickupDelaySeconds = 0.5f;

		/** @brief 使ったとき、対象の最大 HP に対して戻る割合。固定量にしない理由は要件。 */
		float healRatio = 0.3f;

		int32_t bagCapacity = 5;

		/** @brief 底面を地表からこれだけ浮かせる。回収の距離判定には使わない(判定は接地位置で見る)。 */
		float hoverHeightCentimeters = 30.0f;

		/** @brief Y 軸まわりの回転。位相は絶対の経過秒から出す(ADR-043)。0 なら回らない。 */
		float rotationsPerSecond = 1.0f;

		/** @brief 借りたメッシュに掛ける一様倍率。MarkerPyramid 50cm 角 ➡ 75cm 角。 */
		float displayScale = 1.5f;
	};

	/** @brief パーティ共有のバッグ。個数だけを持つ（種類は 1 つしかない、GameRules 5）。 */
	struct ItemBag
	{
		FANG_REFLECT_BEGIN(ItemBag)
		FANG_FIELD(count, "個数", Range(0.0f, 99.0f))
		FANG_REFLECT_END()

		int32_t count = 0;
	};

	/**
	 * @brief 整数を撹拌する。Murmur3 の最終段（fmix32）そのまま。
	 * @details 乱数器ではない。状態を持たず、同じ値には必ず同じ答えを返す。
	 */
	[[nodiscard]] uint32_t HashInteger(uint32_t value);

	/**
	 * @brief 通算番号から 0 以上 1 未満の値を作る。
	 * @details 上位 24 ビットだけを使う。32 ビットのまま float へ落とすと、最大値が 1.0f へ丸まって
	 *          「率 1 でも落ちない番号」が出るため。
	 */
	[[nodiscard]] float ComputeDropRatio(uint32_t serialNumber);

	/** @brief この撃破で落ちるか。率 0 なら必ず false、率 1 なら必ず true。 */
	[[nodiscard]] bool ShouldDropItem(const ItemDropParameter& parameter, uint32_t defeatSerialNumber);

	/** @brief バッグへ 1 個入れる。上限に達していれば false（個数は動かない）。 */
	[[nodiscard]] bool AddItemToBag(const ItemDropParameter& parameter, ItemBag* bag);

	/** @brief バッグから 1 個取り出す。0 個なら false（負にしない）。 */
	[[nodiscard]] bool TakeItemFromBag(ItemBag* bag);

	/**
	 * @brief 席ごとの残り秒数を 1 フレームぶん減らす。
	 * @return このフレームに尽きた席のビット（席 0 が最下位）。既に 0 の席は立たない。
	 * @details 残り 0 が「空き席」。減らした結果は 0 未満にしない。席は 32 個まで。
	 */
	[[nodiscard]] uint32_t StepItemLifetimes(std::span<float> remainingSecondsPerSlot, float deltaTimeSeconds);

	/**
	 * @brief 新しく置く席を選ぶ。
	 * @return 空き席があればその番号。無ければ残りが最も少ない席（最も古い）。空の span なら size()。
	 */
	[[nodiscard]] uint32_t SelectItemSlot(std::span<const float> remainingSecondsPerSlot);

	/** @brief 回収できる距離にあるか。高さも含めた 3 次元の距離で見る（どちらも接地済みの位置）。 */
	[[nodiscard]] bool IsWithinPickupRange(
		const ItemDropParameter& parameter,
		const Vector3&           itemPosition,
		const Vector3&           collectorPosition
	);

	/**
	 * @brief 落ちてからの経過が待ちを超えたか。
	 * @param remainingSeconds StepItemLifetimes が減らした後の残り。0(空き席)は常に false。
	 * @details 経過 = 寿命 − 残り。待ちが寿命以上なら一度も true にならず、寿命で消える(無限ループしない)。
	 *          待ち 0 なら落ちた次の周から true(今までと同じ)。丸め残りは寿命と同じ 1.0e-4 秒で吸収する。
	 */
	[[nodiscard]] bool IsItemReadyForPickup(const ItemDropParameter& parameter, float remainingSeconds);

	/**
	 * @brief 落ちた物の見た目の行列。接地位置から浮かせ、絶対の経過秒で回し、一様に拡大する。
	 * @param groundPosition 席に写した接地位置。回収の距離判定に使うのと同じ点。
	 * @param elapsedSeconds 起動からの絶対秒。積まない ➡ 上限で切られた周があっても位相が実時間からずれない(ADR-043)。
	 * @return 拡大 ➡ Y 回転 ➡ 平行移動(x, y + 浮かせる高さ, z)。
	 */
	[[nodiscard]] Matrix4x4 ComputeItemDisplayMatrix(
		const ItemDropParameter& parameter,
		const Vector3&           groundPosition,
		double                   elapsedSeconds
	);
} // namespace fang
