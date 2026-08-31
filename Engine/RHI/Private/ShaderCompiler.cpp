/**
 * @file ShaderCompiler.cpp
 * @brief HLSL の実行時コンパイル。
 */
#include "Pch.h"
#include "RHI/ShaderCompiler.h"
#include "Core/Log/Assert.h"
#include "RHI/RHILog.h"
#include <string>

#if FANG_PLATFORM_WINDOWS
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace fang::rhi
{
#if FANG_PLATFORM_WINDOWS

	bool CompileShaderFromSource(std::string_view source,
								 std::string_view entryPointName,
								 EnShaderStage stage,
								 std::vector<uint8_t>* outBytecode)
	{
		FANG_ASSERT(outBytecode != nullptr, "出力先が nullptr");

		const char* targetName = stage == EnShaderStage::Vertex ? "vs_5_1" : "ps_5_1";

		UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if FANG_DEBUG
		compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		const std::string entryPoint(entryPointName);

		Microsoft::WRL::ComPtr<ID3DBlob> compiled;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		const HRESULT result = ::D3DCompile(source.data(),
											source.size(),
											nullptr,
											nullptr,
											nullptr,
											entryPoint.c_str(),
											targetName,
											compileFlags,
											0,
											&compiled,
											&errors);

		if (FAILED(result))
		{
			const std::string_view message =
				errors != nullptr
					? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
					: std::string_view("(エラーメッセージなし)");
			FANG_LOG_ERROR(RHI, "シェーダーのコンパイルに失敗した: {}", message);
			return false;
		}

		const uint8_t* begin = static_cast<const uint8_t*>(compiled->GetBufferPointer());
		// TODO: Core の Array<T> ができたら std::vector をやめる。
		outBytecode->assign(begin, begin + compiled->GetBufferSize());

		return true;
	}

#else

	bool CompileShaderFromSource(std::string_view, std::string_view, EnShaderStage, std::vector<uint8_t>*)
	{
		// UWP には d3dcompiler が無い。ビルド時コンパイル（DXC）に切り替えるまでは Xbox で描けない。
		FANG_LOG_ERROR(RHI, "UWP では実行時シェーダーコンパイルが使えない");
		return false;
	}

#endif
} // namespace fang::rhi
