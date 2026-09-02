/**
 * @file AnimationPlaybackTests.cpp
 * @brief 再生位置のテスト。実時間との対応、ループの継ぎ目、後ろ向き再生、尺が無いときの扱い。
 */
#include "Animation/AnimationPlayback.h"
#include <doctest.h>


namespace
{
	/** @brief 24fps で書き出した 24 フレームのクリップ。狼の歩きと同じ長さにしてある。 */
	constexpr float CLIP_DURATION_SECONDS = 1.0f;

	/** @brief 60fps 1 フレームぶんの経過時間。 */
	constexpr float FRAME_DELTA_SECONDS = 1.0f / 60.0f;

	/** @brief 秒どうしの比較に使う許容差。float で 1 秒を 60 分割して積むので、この桁で足りる。 */
	constexpr float TIME_TOLERANCE = 1.0e-4f;
} // namespace


TEST_CASE("経過時間ぶんだけ進み、尺との比が 0〜1 に収まる")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(CLIP_DURATION_SECONDS);

	CHECK(playback.GetTimeSeconds() == 0.0f);
	CHECK(playback.GetTimeRatio() == 0.0f);

	playback.Advance(0.25f);
	CHECK(playback.GetTimeSeconds() == doctest::Approx(0.25f).epsilon(TIME_TOLERANCE));
	CHECK(playback.GetTimeRatio() == doctest::Approx(0.25f).epsilon(TIME_TOLERANCE));

	playback.Advance(0.5f);
	CHECK(playback.GetTimeRatio() == doctest::Approx(0.75f).epsilon(TIME_TOLERANCE));
}


TEST_CASE("末尾を越えたら先頭からの続きになる")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(CLIP_DURATION_SECONDS);

	playback.Advance(0.9f);
	playback.Advance(0.2f);

	// 0.9 + 0.2 = 1.1 ➡ 1 周ぶん引いて 0.1。切り捨てて 0 に戻すと継ぎ目で姿勢が飛ぶ。
	CHECK(playback.GetTimeSeconds() == doctest::Approx(0.1f).epsilon(TIME_TOLERANCE));
}


TEST_CASE("1 フレームで何周ぶん飛んでも位置が正しく残る")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(CLIP_DURATION_SECONDS);

	// 処理落ちで 3.25 秒ぶん飛んだ場合。3 周ぶんを捨てて 0.25 が残る。
	playback.Advance(3.25f);
	CHECK(playback.GetTimeSeconds() == doctest::Approx(0.25f).epsilon(TIME_TOLERANCE));
	CHECK(playback.GetTimeRatio() < 1.0f);
}


TEST_CASE("刻み方が違っても同じ実時間なら同じ位置に来る")
{
	fang::AnimationPlayback fineGrained;
	fang::AnimationPlayback coarseGrained;
	fineGrained.SetDurationSeconds(CLIP_DURATION_SECONDS);
	coarseGrained.SetDurationSeconds(CLIP_DURATION_SECONDS);

	// 60fps で 30 フレームぶん進めた側と、0.5 秒を 1 回で進めた側。
	// フレームレートが変動しても再生速度が変わらないことを、この一致で見る。
	for (int frame = 0; frame < 30; ++frame)
	{
		fineGrained.Advance(FRAME_DELTA_SECONDS);
	}
	coarseGrained.Advance(0.5f);

	CHECK(fineGrained.GetTimeSeconds() == doctest::Approx(coarseGrained.GetTimeSeconds()).epsilon(TIME_TOLERANCE));
}


TEST_CASE("再生の速さが実時間との対応を変える")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(CLIP_DURATION_SECONDS);
	playback.SetPlaybackSpeed(2.0f);

	playback.Advance(0.25f);
	CHECK(playback.GetTimeSeconds() == doctest::Approx(0.5f).epsilon(TIME_TOLERANCE));
}


TEST_CASE("後ろ向きに再生すると先頭を越えて末尾へ回る")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(CLIP_DURATION_SECONDS);
	playback.SetPlaybackSpeed(-1.0f);

	playback.Advance(0.25f);

	// 0 - 0.25 は負になる。1 周ぶん足して 0.75 にしないと、次のサンプリングが範囲外の比を受け取る。
	CHECK(playback.GetTimeSeconds() == doctest::Approx(0.75f).epsilon(TIME_TOLERANCE));
	CHECK(playback.GetTimeRatio() >= 0.0f);
}


TEST_CASE("尺を渡していなければ何も起きない")
{
	fang::AnimationPlayback playback;

	playback.Advance(1.0f);

	// .ozz を読めなかったときにここへ来る。呼び出し側に分岐を書かずにバインドポーズのままにできる。
	CHECK(playback.GetTimeSeconds() == 0.0f);
	CHECK(playback.GetTimeRatio() == 0.0f);
}


TEST_CASE("尺が縮んでも位置がはみ出さない")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(2.0f);
	playback.Advance(1.5f);

	playback.SetDurationSeconds(1.0f);

	CHECK(playback.GetTimeSeconds() < 1.0f);
	CHECK(playback.GetTimeSeconds() == doctest::Approx(0.5f).epsilon(TIME_TOLERANCE));
}


TEST_CASE("Rewind で先頭へ戻り、速さは変わらない")
{
	fang::AnimationPlayback playback;
	playback.SetDurationSeconds(CLIP_DURATION_SECONDS);
	playback.SetPlaybackSpeed(0.5f);
	playback.Advance(0.4f);

	playback.Rewind();

	CHECK(playback.GetTimeSeconds() == 0.0f);
	CHECK(playback.GetPlaybackSpeed() == 0.5f);
}
