/**
 * @file AnimationPlayback.h
 * @brief クリップ 1 本の再生位置。
 */
#pragma once

#include "Core/CoreMacros.h"


namespace fang
{
	/**
	 * @brief クリップの再生位置を実時間で進め、末尾まで来たら先頭へ戻す。
	 * @details 骨も姿勢も知らない。ここが持つのは「今どこを再生しているか」だけで、そのぶん描画なしで
	 *          確かめられる。➡ループの継ぎ目や、フレームレートが変動したときの進み方はテストで押さえる。
	 *          尺を渡していない間は何も起きず、位置は 0 のまま。読み込みに失敗しても呼び出し側に分岐が要らない。
	 * @threading メインスレッドのみ。
	 */
	class AnimationPlayback
	{
	public:
		/**
		 * @brief クリップの長さを秒で渡す。
		 * @details 今の位置が新しい尺からはみ出したら、はみ出したぶんだけ先頭側へ巻き戻す。
		 * @param durationSeconds 0 以下なら「尺が無い」扱いになり、以後 Advance は何もしない。
		 */
		void SetDurationSeconds(float durationSeconds);

		/**
		 * @brief 再生の速さ。1.0 でクリップの尺どおり。
		 * @details 実時間との対応を変えるのはここだけ。負の値を渡すと後ろ向きに再生し、先頭を越えたら末尾へ回る。
		 */
		FANG_FORCEINLINE void SetPlaybackSpeed(float speed) { m_playbackSpeed = speed; }

		/**
		 * @brief 経過時間ぶん進める。末尾を越えたぶんは先頭からの続きになる。
		 * @param deltaTimeSeconds 前のフレームからの実時間。負の値は無視する。
		 * @details 1 フレームで尺を何周ぶんも飛ばしても位置が正しく残る。➡処理落ちの後で姿勢が飛ばない。
		 */
		void Advance(float deltaTimeSeconds);

		/** @brief 先頭へ戻す。速さは変えない。 */
		FANG_FORCEINLINE void Rewind() { m_timeSeconds = 0.0f; }

		/** @brief 今の再生位置（秒）。0 以上、尺未満。 */
		[[nodiscard]] FANG_FORCEINLINE float GetTimeSeconds() const { return m_timeSeconds; }

		/** @brief クリップの長さ（秒）。渡していなければ 0。 */
		[[nodiscard]] FANG_FORCEINLINE float GetDurationSeconds() const { return m_durationSeconds; }

		/** @brief 再生の速さ。 */
		[[nodiscard]] FANG_FORCEINLINE float GetPlaybackSpeed() const { return m_playbackSpeed; }

		/**
		 * @brief 再生位置を 0〜1 で表したもの。
		 * @details ozz のサンプリングがこの形で受け取るので、割り算をここに閉じる。尺が無ければ 0。
		 */
		[[nodiscard]] float GetTimeRatio() const;


	private:
		float m_durationSeconds = 0.0f;
		float m_timeSeconds     = 0.0f;
		float m_playbackSpeed   = 1.0f;
	};
} // namespace fang
