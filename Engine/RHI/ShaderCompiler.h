/**
 * @file ShaderCompiler.h
 * @brief HLSL を実行時にコンパイルする。
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


namespace fang::rhi
{
	/** @brief シェーダーの種類。 */
	enum class EnShaderStage : uint8_t
	{
		Vertex,
		Pixel,
	};

	/** @brief ファイルからのコンパイルの結末。 */
	enum class EnShaderCompileResult : uint8_t
	{
		Success,
		FileUnavailable, /**< 開けなかった。保存の途中を掴んだ見込み ➡ 呼ぶ側は少し待ってやり直す。 */
		CompileFailed,   /**< 中身が通らなかった。理由は outErrorMessage に入る。 */
	};

	/**
	 * @brief HLSL の文字列をコンパイルしてバイトコードを返す。
	 * @details 実行時コンパイルはホットリロード専用。本番の描画はビルド時に FXC が焼いたバイトコードで動く。
	 *          UWP には d3dcompiler が無く、Release では経路ごと落としてあるので、そこでは必ず失敗する。
	 * @param source           HLSL のソースコード文字列。ファイルパスではなく中身を渡す。
	 * @param entryPointName   シェーダの入口になる関数名。"VertexMain" など。
	 * @param[out] outBytecode 成功したときだけ書き換える。nullptr は不可。
	 * @return 成功したら true。失敗の内容はログに出る。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] bool CompileShaderFromSource(
		std::string_view      source,
		std::string_view      entryPointName,
		EnShaderStage         stage,
		std::vector<uint8_t>* outBytecode
	);

	/**
	 * @brief .hlsl のファイルを読んでコンパイルする。
	 * @details 隣の .hlsli を #include で解決するので、ソースツリーの中のファイルを指すこと。
	 *          パスは非 ASCII を含まない前提（コンパイラへは ANSI として渡るため）。
	 * @param utf8FilePath         .hlsl の絶対パス（UTF-8）。
	 * @param entryPointName       シェーダの入口になる関数名。ビルド時の EntryPointName と同じものを渡す。
	 * @param[out] outBytecode     成功したときだけ書き換える。nullptr は不可。
	 * @param[out] outErrorMessage CompileFailed のときだけ書き換える。どのファイルの何行目かが入る。nullptr 可。
	 * @return 開けなければ FileUnavailable、通らなければ CompileFailed。
	 * @threading 任意のスレッド。
	 */
	[[nodiscard]] EnShaderCompileResult CompileShaderFromFile(
		const char*           utf8FilePath,
		std::string_view      entryPointName,
		EnShaderStage         stage,
		std::vector<uint8_t>* outBytecode,
		std::string*          outErrorMessage
	);
} // namespace fang::rhi
