/**
 * @file MeatManager.h
 * @brief 場に出ている肉の席をまとめて持ち、寿命・回収・落下を進める係。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Scene/Actor.h"
#include "Scene/ItemDrop.h"
#include "WolfTeamItems.h"
#include <array>
#include <cstdint>


namespace fang
{
	class Scene;
} // namespace fang


namespace fang::game
{
	struct WolfModel;

	/**
	 * @brief 場に出ている肉の席をまとめて持ち、寿命・回収・落下を進める係。
	 * @details 肉に振る舞いは持たせない。8 席をまとめてここが見る。1 周の順は 寿命 ➡ 回収 ➡ 落下
	 *          （回収半径が牙の間合いより広いので、落下を先にすると倒したその周のうちに拾われて
	 *          1 フレームも映らない）。距離の判定は席に写した位置で見る
	 *          （Actor::GetWorldPosition は生成した周にはまだ原点を返すため）。
	 * @threading 更新ジョブ 1 本から。
	 */
	class MeatManager
	{
	public:
		/** @brief 場に同時に出せる肉の数。席の予算は 74 + 8 = 82 / 128。 */
		static constexpr uint32_t MAX_MEAT_COUNT = 8;

		/** @brief Game 側が持ち続ける資源への借用。 */
		struct Dependencies
		{
			Scene*           scene       = nullptr;
			const WolfModel* sharedModel = nullptr; /**< 肉のメッシュ。狼のモデルを縮めて流用する。 */

			/** @brief 拾う側（今の操作対象）。全滅中は無効な Actor を指す。 */
			const Actor* collector = nullptr;
		};

		void Update(
			float                    deltaTimeSeconds,
			const ItemDropParameter& parameter,
			WolfTeamItems*           teamItems,
			const Dependencies&      dependencies
		);

		/** @brief 今場に出ている肉の数。残り秒数が 0 より大きい席を数える（8 席）。 */
		[[nodiscard]] uint32_t GetActiveCount() const;


	private:
		std::array<Actor, MAX_MEAT_COUNT>   m_actors;
		std::array<Vector3, MAX_MEAT_COUNT> m_positions;          /**< 席ごとの落ちた位置。 */
		std::array<float, MAX_MEAT_COUNT>   m_remainingSeconds{}; /**< 0 が空き席。 */

		/** @brief 通算の撃破番号。落ちるかどうかを決める種で、1 から始まる。 */
		uint32_t m_defeatSerialNumber = 0;
	};
} // namespace fang::game
