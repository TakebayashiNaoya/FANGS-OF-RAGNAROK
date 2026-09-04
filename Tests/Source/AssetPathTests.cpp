/**
 * @file AssetPathTests.cpp
 * @brief 置き場所の解決のテスト。ソースツリーの根が本当にリポジトリの一番上を指しているか。
 */
#include "Core/Platform/AssetPath.h"
#include "Core/Platform/FileSystem.h"
#include <doctest.h>
#include <cstdio>
#include <string>


TEST_CASE("GetSourceRootPath がソリューションのあるディレクトリを返す")
{
	// 例外を切ってあるので REQUIRE は使えない。確かめてから自分で打ち切る。
	const std::string sourceRootPath = fang::GetSourceRootPath();
	CHECK_FALSE(sourceRootPath.empty());
	if (sourceRootPath.empty())
	{
		return;
	}

	// 末尾に区切りを付けない約束。付いていると呼ぶ側の連結で \\ が二重になる。
	CHECK(sourceRootPath.back() != '\\');
	CHECK(sourceRootPath.back() != '/');

	// 根っこの目印そのものを開けることを確かめる。パスの形だけ合っていても意味が無い。
	const std::string solutionPath = sourceRootPath + "\\FangsOfRagnarok.sln";

	std::FILE* solutionFile = fang::OpenFile(solutionPath.c_str(), "rb");
	CHECK(solutionFile != nullptr);
	if (solutionFile != nullptr)
	{
		std::fclose(solutionFile);
	}
}
