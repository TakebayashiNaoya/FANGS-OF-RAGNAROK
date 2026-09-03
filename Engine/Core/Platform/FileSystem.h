/**
 * @file FileSystem.h
 * @brief ファイルを開く処理の OS 依存部分。
 */
#pragma once

#include <cstdio>


namespace fang
{
	/**
	 * @brief UTF-8 のパスでファイルを開く。
	 * @details narrow 文字列版の fopen / fopen_s は Windows では現在の ANSI コードページとして
	 *          パスを解釈するため、日本語などの非 ASCII を含むパスが化けて開けない。
	 *          ここで UTF-16 に直してから開くことで、エンジン内で決めた「パスは UTF-8」を保ったまま
	 *          正しく開けるようにする。
	 * @param utf8Path 開くファイルの絶対パス（UTF-8）。
	 * @param mode fopen と同じ書式のモード文字列（"rb" など）。ASCII だけを想定する。
	 * @return 開けなければ nullptr。開けたら呼んだ側が fclose で閉じる。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] std::FILE* OpenFile(const char* utf8Path, const char* mode);
} // namespace fang
