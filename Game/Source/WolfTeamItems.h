/**
 * @file WolfTeamItems.h
 * @brief 狼のチームの持ち物と、この周に落ちるものの申し送り。
 */
#pragma once

#include "Core/Math/Vector3.h"
#include "Scene/ItemDrop.h"
#include "Scene/MeleeSwing.h"
#include <cstdint>


namespace fang::game
{
	/**
	 * @brief 狼のチームの持ち物と、この周に落ちるものの申し送り。
	 * @details 実体は WolfManager が 1 つだけ持ち、狼はポインタで借りる（GameRules 5 のパーティ共有
	 *          ➡ 狼が倒れても減らない）。バッグは MeatManager が増やし、狼が減らす。
	 * @threading 更新ジョブ 1 本から。つまみの書き戻しはフレームループが更新の外で入れる（ADR-051）。
	 */
	struct WolfTeamItems
	{
		/** @brief 1 周に申告できる落とし物の数。1 振りで当てられる上限（MAX_MELEE_SWING_HIT_COUNT）と同じ。 */
		static constexpr uint32_t MAX_PENDING_DROP_COUNT = MAX_MELEE_SWING_HIT_COUNT;

		ItemBag bag;

		/** @brief この周に狼が倒した相手が居た位置。MeatManager が読んで空にする。 */
		Vector3  pendingDropPositions[MAX_PENDING_DROP_COUNT];
		uint32_t pendingDropCount = 0;
	};

	/** @brief 落とし物の申し送りを 1 件積む。満杯なら黙って捨てる。 */
	inline void PushPendingDrop(WolfTeamItems* items, const Vector3& position)
	{
		if (items->pendingDropCount >= WolfTeamItems::MAX_PENDING_DROP_COUNT)
		{
			return;
		}

		items->pendingDropPositions[items->pendingDropCount] = position;
		++items->pendingDropCount;
	}
} // namespace fang::game
