/**
 * @file AI.h
 * @brief AI モジュールの入口（感知 / ブラックボード / 意思決定 / 湧き）。
 * @details 公開ヘッダをまとめた傘ヘッダ。.cpp からはこれ 1 本で足りる。
 */
#pragma once

#include "AI/AgentBlackboard.h"
#include "AI/Perception.h"
#include "AI/PursuitStateMachine.h"
#include "AI/SpawnScheduler.h"

namespace fang
{
	/**
	 * @brief モジュール名を返す。
	 * @details 骨格のみ。参照とリンクが通っていることの確認にだけ使う。
	 * @threading 任意のスレッド。
	 */
	const char* GetAIModuleName();
} // namespace fang
