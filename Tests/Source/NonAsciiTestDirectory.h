/**
 * @file NonAsciiTestDirectory.h
 * @brief 非 ASCII パスの読み込みテスト用に、%TEMP% の下へ日本語名のディレクトリを作って後始末する。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <filesystem>
#include <string>
#include <string_view>


namespace fang::test
{
	/**
	 * @brief %TEMP% の下に指定した名前のディレクトリを作り、破棄時に中身ごと削除する。
	 * @details narrow API は現在の ANSI コードページでパスを解釈するため、日本語などの
	 *          非 ASCII を含むパスを正しく読み書きできるかを確かめるテストで使う。
	 * @threading テストケースの中で 1 つずつ直列に使う想定。並行実行はしない。
	 */
	class NonAsciiTestDirectory
	{
	public:
		FANG_NON_COPYABLE(NonAsciiTestDirectory);
		FANG_NON_MOVABLE(NonAsciiTestDirectory);

		/**
		 * @brief %TEMP% の下に directoryName のディレクトリを作る。
		 * @param directoryName 作るディレクトリの名前。
		 */
		explicit NonAsciiTestDirectory(const wchar_t* directoryName)
		{
			m_path = std::filesystem::temp_directory_path() / directoryName;
			std::filesystem::create_directories(m_path);
		}

		/** @brief ディレクトリを中身ごと削除する。 */
		~NonAsciiTestDirectory() { std::filesystem::remove_all(m_path); }

		/** @brief ディレクトリ自身の絶対パスを UTF-8 で返す。末尾に区切り文字は付けない。 */
		[[nodiscard]] std::string GetPath() const
		{
			const std::u8string directoryUtf8 = m_path.u8string();
			return std::string(reinterpret_cast<const char*>(directoryUtf8.data()), directoryUtf8.size());
		}

		/**
		 * @brief ディレクトリの下にあるファイルの絶対パスを UTF-8 で返す。
		 * @details fileName は文字列の結合でそのまま繋ぐ。std::filesystem::path の narrow 文字列側の
		 *          変換は現在のロケール（ACP）を経由し、UTF-8 の日本語バイト列を渡すと変換に失敗して
		 *          落ちる（このプロジェクトは例外を切っているため、内部の throw が abort になる）。
		 * @param fileName UTF-8 のファイル名。ASCII に限らず日本語などを含んでもよい。
		 */
		[[nodiscard]] std::string MakeFilePath(std::string_view fileName) const
		{
			std::string path = GetPath();
			path += '\\';
			path += fileName;
			return path;
		}


	private:
		std::filesystem::path m_path;
	};
} // namespace fang::test
