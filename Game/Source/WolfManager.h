/**
 * @file WolfManager.h
 * @brief 狼の席と、今どれを操作しているかを 1 か所で持つ入れ物。
 */
#pragma once

#include "Scene/Scene.h"
#include <array>
#include <cstdint>


namespace fang::game
{
	class WolfController;

	/** @brief 1 フレームぶんの答え。 */
	struct WolfManagerUpdateResult
	{
		uint32_t aliveCount = 0;
		bool     didWipeOut = false; /**< このフレームに全滅した。立ち上がりの 1 回だけ true。 */
	};

	/**
	 * @brief 狼の席と、今どれを操作しているか。
	 * @details 操作・カメラ・湧きの基準・雑魚の標的はすべてここから引く（指す先が 1 つなので、
	 *          引き継ぎで揃わないことが起きない）。振る舞いの生ポインタは世代付きハンドルと対で持ち、
	 *          使う前に必ず Scene::IsValid を通す（ADR-038）。
	 * @threading 更新ジョブ 1 本から。
	 */
	class WolfManager
	{
	public:
		/** @brief 席の数。GameRules 1 の 9 匹まで入る大きさ。今 Game が作るのは 2 体。 */
		static constexpr uint32_t MAX_WOLF_COUNT = 9;

		/** @brief 生成した狼を席に加える。上限を超えたら false（何もしない）。 */
		[[nodiscard]] bool Add(ActorHandle handle, WolfController* controller);

		/**
		 * @brief 生死を数え直し、操作対象を選び直す。
		 * @details 周の頭で、振る舞いのポインタを誰かが触るより前に呼ぶこと。撃破された狼の
		 *          ポインタはここで捨てる（ブロックは前の周の破棄反映で解放済み）。
		 */
		WolfManagerUpdateResult Update(const Scene& scene);

		/** @brief 今の操作対象。生きていなければ無効なハンドルを指す。雑魚の標的に渡す。 */
		[[nodiscard]] const ActorHandle* GetControlledHandle() const { return &m_controlledHandle; }

		/** @brief 今の操作対象の振る舞い。生きていなければ nullptr。 */
		[[nodiscard]] WolfController* GetControlledWolf() const { return m_controlledWolf; }


	private:
		std::array<ActorHandle, MAX_WOLF_COUNT>     m_handles;
		std::array<WolfController*, MAX_WOLF_COUNT> m_controllers{};
		uint32_t                                    m_count = 0;

		ActorHandle     m_controlledHandle;
		WolfController* m_controlledWolf = nullptr;
		bool            m_wasWipedOut    = false;
	};
} // namespace fang::game
