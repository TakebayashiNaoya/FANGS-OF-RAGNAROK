/**
 * @file DirectoryWatcher.h
 * @brief ディレクトリの中身が変わったかを見張る。
 */
#pragma once

#include "Core/CoreMacros.h"


namespace fang
{
	/**
	 * @brief 1 つのディレクトリの変化を見張る。
	 * @details 直下のファイルの追加・削除・改名・書き込みを「変化あり」の 1 つにまとめて返す。
	 *          どのファイルが変わったかは分からない ➡ 変化を知った側がそのディレクトリを出どころとするものを
	 *          まとめて作り直す使い方を想定している。
	 *          UWP はパッケージの外を見張れないので、Initialize が必ず失敗する。
	 * @threading メインスレッドのみ。
	 */
	class DirectoryWatcher
	{
	public:
		FANG_NON_COPYABLE(DirectoryWatcher);

		DirectoryWatcher() = default;
		~DirectoryWatcher();

		/**
		 * @brief 見張りを始める。
		 * @param utf8DirectoryPath 見張るディレクトリの絶対パス（UTF-8）。中の子ディレクトリは見ない。
		 * @return 開けなければ false。二重に呼んだときは前の見張りを閉じてから開き直す。
		 */
		[[nodiscard]] bool Initialize(const char* utf8DirectoryPath);

		/** @brief 見張りをやめる。二重に呼んでも安全。 */
		void Shutdown();

		/** @brief 見張れているか。Initialize が通っていれば true。 */
		[[nodiscard]] FANG_FORCEINLINE bool IsWatching() const { return m_nativeHandle != nullptr; }

		/**
		 * @brief 前回の問い合わせ以降に変化していたら true を返し、変化を消費する。
		 * @details その場で見るだけで待たないので、毎フレーム呼んでよい。ファイルの中身は読まない。
		 * @return 見張れていなければ常に false。
		 */
		[[nodiscard]] bool ConsumeChange();


	private:
		/** @brief 変化通知のハンドル。Windows なら FindFirstChangeNotificationW が返したもの。 */
		void* m_nativeHandle = nullptr;
	};
} // namespace fang
