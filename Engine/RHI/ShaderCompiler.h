/**
 * @file ShaderCompiler.h
 * @brief HLSL を実行時にコンパイルする。
 */
#pragma once

#include <cstdint>
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

	/**
	 * @brief HLSL の文字列をコンパイルしてバイトコードを返す。
	 * @details Phase 1 の暫定。ビルド時コンパイル（DXC）にするかは未決。
	 *          UWP では d3dcompiler が使えないので、Xbox 構成では必ず失敗する。
	 * @param source           HLSL のソースコード文字列。ファイルパスではなく中身を渡す。
	 * @param entryPointName   シェーダの入口になる関数名。"VSMain" など。
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
} // namespace fang::rhi
