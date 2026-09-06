/**
 * @file WolfManager.h
 * @brief 狼の席と、今どれを操作しているかを 1 か所で持つ入れ物。
 */
#pragma once

#include "Scene/Actor.h"
#include "WolfTeamGrowth.h"
#include <array>
#include <cstdint>


namespace fang::game
{
	class WolfController;

	/** @brief 1 フレームぶんの答え。 */
	struct WolfManagerUpdateResult
	{
		uint32_t aliveCount       = 0;
		bool     didWipeOut       = false; /**< このフレームに全滅した。立ち上がりの 1 回だけ true。 */
		int32_t  gainedLevelCount = 0;     /**< この周にチームが上がった段の数。0 なら上がっていない。 */
	};

	/**
	 * @brief 狼の席と、今どれを操作しているか。
	 * @details 操作・カメラ・湧きの基準・雑魚の標的はすべてここから引く（指す先が 1 つなので、
	 *          引き継ぎで揃わないことが起きない）。振る舞いの生ポインタは窓（Actor）と対で持ち、
	 *          使う前に必ず IsValid を通す（ADR-038）。
	 * @threading 更新ジョブ 1 本から。
	 */
	class WolfManager
	{
	public:
		/** @brief 席の数。GameRules 1 の 9 匹まで入る大きさ。今 Game が作るのは 2 体。 */
		static constexpr uint32_t MAX_WOLF_COUNT = 9;

		/** @brief 生成した狼を席に加える。上限を超えたら false（何もしない）。 */
		[[nodiscard]] bool Add(Actor actor, WolfController* controller);

		/**
		 * @brief 生死を数え直し、操作対象を選び直す。
		 * @details 周の頭で、振る舞いのポインタを誰かが触るより前に呼ぶこと。撃破された狼の
		 *          ポインタはここで捨てる（ブロックは前の周の破棄反映で解放済み）。
		 */
		WolfManagerUpdateResult Update();

		/** @brief 今の操作対象。生きていなければ無効な Actor を指す。雑魚の標的にも渡す。 */
		[[nodiscard]] const Actor* GetControlledActor() const { return &m_controlledActor; }

		/** @brief 今の操作対象の振る舞い。生きていなければ nullptr。 */
		[[nodiscard]] WolfController* GetControlledWolf() const { return m_controlledWolf; }

		/** @brief チームの成長。狼が借りるので、寿命はここが持つ。 */
		[[nodiscard]] WolfTeamGrowth* GetTeamGrowth() { return &m_teamGrowth; }

		/** @brief 自分が持つ調整値（チームの成長）を登録簿へ載せる。呼ぶかどうかは呼び出し側が決める。 */
		void RegisterTuningValues();


	private:
		std::array<Actor, MAX_WOLF_COUNT>           m_actors;
		std::array<WolfController*, MAX_WOLF_COUNT> m_controllers{};
		uint32_t                                    m_count = 0;

		Actor           m_controlledActor;
		WolfController* m_controlledWolf = nullptr;
		bool            m_wasWipedOut    = false;

		/** @brief チームの経験値・レベル・倍率。撃破の申告も含めてここへ集める。 */
		WolfTeamGrowth m_teamGrowth;
	};
} // namespace fang::game
