/**
 * @file AssetPath.h
 * @brief アセットを置いた場所の解決。
 */
#pragma once

#include <string>


namespace fang
{
	/**
	 * @brief アセットを置いた根っこのディレクトリを返す。
	 * @details 置き場所はプラットフォームで違う。Windows は exe の隣の Assets、
	 *          UWP はパッケージの InstalledLocation の下の Assets。
	 * @return UTF-8 の絶対パス。末尾に区切り文字は付けない。見つからなければ空文字列。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] std::string GetAssetRootPath();

	/**
	 * @brief 根っこからの相対パスを絶対パスに直す。
	 * @details 中身は文字列を繋ぐだけでプラットフォームに依らないので、ここに置く。
	 *          .cpp に出すと Windows\AssetPath.cpp と同名になり、.obj の出力先が衝突する。
	 * @param relativePath 根っこからの相対パス。先頭の区切りはあってもなくてもよく、
	 *                     / と \ のどちらでも受ける。nullptr か空文字列なら根っこを返す。
	 * @return UTF-8 の絶対パス。根っこが取れなければ空文字列。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] inline std::string MakeAssetPath(const char* relativePath)
	{
		std::string path = GetAssetRootPath();
		if (path.empty())
		{
			return std::string();
		}

		if (relativePath == nullptr || relativePath[0] == '\0')
		{
			return path;
		}

		// 呼ぶ側が "Models\Wolf.gltf" とも "\Models\Wolf.gltf" とも書けるようにする。
		// 区切りは Windows も UWP も \ なので、プラットフォームで分ける必要が無い。
		if (relativePath[0] != '\\' && relativePath[0] != '/')
		{
			path += '\\';
		}

		path += relativePath;
		return path;
	}
} // namespace fang
