/**
 * @file ShaderCompiler.cpp
 * @brief HLSL の実行時コンパイル。ホットリロードを切ってある構成では経路ごと消える。
 */
#include "Pch.h"
#include "RHI/ShaderCompiler.h"
#include "Core/Log/Assert.h"
#include "Core/Platform/FileSystem.h"
#include "RHI/RHILog.h"
#include <cstdio>
#include <string>

// Release から d3dcompiler への依存を外すために、リンク指定ごとホットリロードの有無で囲む。
#if FANG_PLATFORM_WINDOWS && FANG_ENABLE_HOT_RELOAD
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace fang::rhi
{
#if FANG_PLATFORM_WINDOWS && FANG_ENABLE_HOT_RELOAD

	namespace
	{
		/**
		 * @brief D3DCompile を呼んでバイトコードを取り出す。
		 * @param sourceName エラー文に出る名前で、#include の探し先の基準にもなる。nullptr なら #include は使えない。
		 */
		bool CompileSource(
			std::string_view      source,
			const char*           sourceName,
			std::string_view      entryPointName,
			EnShaderStage         stage,
			std::vector<uint8_t>* outBytecode,
			std::string*          outErrorMessage
		)
		{
			FANG_ASSERT(outBytecode != nullptr, "出力先が nullptr");

			// ビルド時の FXCompile と同じコンパイラ・同じシェーダモデル。
			// ずらすと「リロードでは出るがビルドすると出ない」画を作り込むことになる。
			const char* targetName = stage == EnShaderStage::Vertex ? "vs_5_1" : "ps_5_1";

			UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if FANG_DEBUG
			compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

			const std::string entryPoint(entryPointName);

			Microsoft::WRL::ComPtr<ID3DBlob> compiled;
			Microsoft::WRL::ComPtr<ID3DBlob> errors;
			const HRESULT                    result = ::D3DCompile(
				source.data(),
				source.size(),
				sourceName,
				nullptr,
				sourceName != nullptr ? D3D_COMPILE_STANDARD_FILE_INCLUDE : nullptr,
				entryPoint.c_str(),
				targetName,
				compileFlags,
				0,
				&compiled,
				&errors
			);

			if (FAILED(result))
			{
				std::string_view message("(エラーメッセージなし)");
				if (errors != nullptr)
				{
					// blob の大きさは終端の '\0' を含むことがある。そのまま繋ぐと表示が途切れるので落とす。
					const char* errorText = static_cast<const char*>(errors->GetBufferPointer());
					size_t      length    = errors->GetBufferSize();
					while (length > 0 && errorText[length - 1] == '\0')
					{
						--length;
					}

					message = std::string_view(errorText, length);
				}

				if (outErrorMessage != nullptr)
				{
					outErrorMessage->assign(message);
				}

				FANG_LOG_ERROR(RHI, "シェーダーのコンパイルに失敗した: {}", message);
				return false;
			}

			const uint8_t* begin = static_cast<const uint8_t*>(compiled->GetBufferPointer());
			// TODO: Core の Array<T> ができたら std::vector をやめる。
			outBytecode->assign(begin, begin + compiled->GetBufferSize());

			return true;
		}


		/**
		 * @brief ファイルを丸ごと読む。
		 * @return 開けない・読み切れないなら false。保存の途中を掴んだときもここに落ちる。
		 */
		bool ReadFileText(const char* utf8FilePath, std::string* outText)
		{
			std::FILE* file = OpenFile(utf8FilePath, "rb");
			if (file == nullptr)
			{
				return false;
			}

			if (std::fseek(file, 0, SEEK_END) != 0)
			{
				std::fclose(file);
				return false;
			}

			const long sizeInBytes = std::ftell(file);
			if (sizeInBytes <= 0)
			{
				std::fclose(file);
				return false;
			}

			std::rewind(file);

			outText->resize(static_cast<size_t>(sizeInBytes));
			const size_t readCount = std::fread(outText->data(), 1, static_cast<size_t>(sizeInBytes), file);
			std::fclose(file);

			return readCount == static_cast<size_t>(sizeInBytes);
		}
	} // namespace


	bool CompileShaderFromSource(
		std::string_view      source,
		std::string_view      entryPointName,
		EnShaderStage         stage,
		std::vector<uint8_t>* outBytecode
	)
	{
		return CompileSource(source, nullptr, entryPointName, stage, outBytecode, nullptr);
	}


	EnShaderCompileResult CompileShaderFromFile(
		const char*           utf8FilePath,
		std::string_view      entryPointName,
		EnShaderStage         stage,
		std::vector<uint8_t>* outBytecode,
		std::string*          outErrorMessage
	)
	{
		FANG_ASSERT(outBytecode != nullptr, "出力先が nullptr");

		std::string source;
		if (utf8FilePath == nullptr || !ReadFileText(utf8FilePath, &source))
		{
			return EnShaderCompileResult::FileUnavailable;
		}

		// パスをそのまま渡すと、隣の .hlsli が #include で解決され、エラー文にもファイル名と行が入る。
		if (!CompileSource(source, utf8FilePath, entryPointName, stage, outBytecode, outErrorMessage))
		{
			return EnShaderCompileResult::CompileFailed;
		}

		return EnShaderCompileResult::Success;
	}


#else

	bool CompileShaderFromSource(std::string_view, std::string_view, EnShaderStage, std::vector<uint8_t>*)
	{
		// UWP には d3dcompiler が無く、Release では依存ごと外してある。描画はビルド時のバイトコードで動く。
		FANG_LOG_ERROR(RHI, "この構成では実行時シェーダーコンパイルが使えない");
		return false;
	}


	EnShaderCompileResult CompileShaderFromFile(
		const char*,
		std::string_view,
		EnShaderStage,
		std::vector<uint8_t>*,
		std::string* outErrorMessage
	)
	{
		if (outErrorMessage != nullptr)
		{
			outErrorMessage->assign("この構成では実行時シェーダーコンパイルが使えない");
		}

		return EnShaderCompileResult::CompileFailed;
	}

#endif
} // namespace fang::rhi
