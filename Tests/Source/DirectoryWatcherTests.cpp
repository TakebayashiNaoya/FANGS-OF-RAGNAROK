/**
 * @file DirectoryWatcherTests.cpp
 * @brief ディレクトリ監視のテスト。変化を拾えるか、消費した後に false へ戻るか、次の変化も拾えるか。
 */
#include "Core/Platform/DirectoryWatcher.h"
#include "Core/Platform/FileSystem.h"
#include "NonAsciiTestDirectory.h"
#include <doctest.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>


namespace
{
	/** @brief 変化が届くのを待つ上限。OS の通知は即時ではないので、回数を区切って見に行く。 */
	constexpr int CHANGE_POLL_COUNT = 100;

	/** @brief 1 回の待ち時間。上限との積（約 1 秒）が変化を諦めるまでの時間になる。 */
	constexpr std::chrono::milliseconds CHANGE_POLL_INTERVAL{ 10 };


	/** @brief 中身が 1 行だけのファイルを作る。失敗したら false。 */
	bool WriteTestFile(const std::string& utf8Path)
	{
		std::FILE* file = fang::OpenFile(utf8Path.c_str(), "wb");
		if (file == nullptr)
		{
			return false;
		}

		const char text[] = "shader hot reload";
		std::fwrite(text, 1, sizeof(text) - 1, file);
		std::fclose(file);
		return true;
	}


	/** @brief 変化が来るまで待つ。来たら true、上限まで来なければ false。 */
	bool WaitForChange(fang::DirectoryWatcher* watcher)
	{
		for (int attempt = 0; attempt < CHANGE_POLL_COUNT; ++attempt)
		{
			if (watcher->ConsumeChange())
			{
				return true;
			}

			std::this_thread::sleep_for(CHANGE_POLL_INTERVAL);
		}

		return false;
	}


	/**
	 * @brief 直前の書き込みが起こした通知を全部消費する。
	 * @details 1 回の保存でも「名前の追加」と「中身の書き込み」が別々に届くことがある。
	 *          変化が無いことを確かめる前に、残っているぶんを流し切っておく。
	 */
	void DrainChanges(fang::DirectoryWatcher* watcher)
	{
		for (int attempt = 0; attempt < CHANGE_POLL_COUNT; ++attempt)
		{
			std::this_thread::sleep_for(CHANGE_POLL_INTERVAL);
			if (!watcher->ConsumeChange())
			{
				return;
			}
		}
	}
} // namespace


TEST_CASE("DirectoryWatcher がファイルの書き込みを拾う")
{
	fang::test::NonAsciiTestDirectory directory(L"ディレクトリ監視テスト_日本語パス");

	// 例外を切ってあるので REQUIRE は使えない。確かめてから自分で打ち切る。
	fang::DirectoryWatcher watcher;
	CHECK(watcher.Initialize(directory.GetPath().c_str()));
	if (!watcher.IsWatching())
	{
		return;
	}

	// 見張りを立てた後の変化だけが届く ➡ ここから書く。
	CHECK(WriteTestFile(directory.MakeFilePath("Mesh.hlsl")));
	CHECK(WaitForChange(&watcher));

	// 変化が無ければ false。保存していない間に作り直しが走らないことがこれで担保される。
	DrainChanges(&watcher);
	CHECK_FALSE(watcher.ConsumeChange());

	// 消費した後も見張りは続く（2 回目の保存も拾える）。
	CHECK(WriteTestFile(directory.MakeFilePath("Terrain.hlsl")));
	CHECK(WaitForChange(&watcher));

	watcher.Shutdown();
	CHECK_FALSE(watcher.IsWatching());
	CHECK_FALSE(watcher.ConsumeChange());
}


TEST_CASE("DirectoryWatcher は無いディレクトリで失敗する")
{
	fang::DirectoryWatcher watcher;
	CHECK_FALSE(watcher.Initialize("C:\\存在しないはずのディレクトリ_FangsOfRagnarok"));
	CHECK_FALSE(watcher.Initialize(nullptr));
	CHECK_FALSE(watcher.IsWatching());
	CHECK_FALSE(watcher.ConsumeChange());
}
