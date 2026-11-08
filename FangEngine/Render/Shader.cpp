/**
 * @file Shader.cpp
 * @brief シェーダーを読み込むクラスの実装
 */
#include "Shader.h"
#include <filesystem>
#include <Windows.h>
#include <fstream>
#include <string>
#include "Core/Log.h"


namespace fangEngine::render
{
	bool Shader::Load(const wchar_t* fileName)
	{
		// exeのパスを取得する
		wchar_t exePath[MAX_PATH]{};
		DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		if (length == 0 || length == MAX_PATH) {
			core::LogError(L"exeの場所の取得に失敗しました。");
			return false;
		}

		// exeのパスからファイル名を消す。
		// →「/Shaders」を付けてShadersフォルダのパスを作る。(exeとShadersフォルダは同じ階層にあるため)
		// →「/fileName」を付けて.csoのフルパスを作る。
		std::filesystem::path shaderPath = std::filesystem::path(exePath).parent_path() / L"Shaders" / fileName;

		// .csoは文字ではなくバイトの並び。
		// std::ios::binaryを付けないとWindowsはファイルを文字として読んでしまう。
		std::ifstream file(shaderPath, std::ios::binary);
		if (file.is_open() == false) {
			std::wstring message = L"シェーダーのファイルを開けませんでした。" + shaderPath.wstring();
			core::LogError(message.c_str());
			return false;
		}

		// m_byteCodeをcsoのファイルサイズだけ広げる
		m_byteCode.resize(std::filesystem::file_size(shaderPath));
		// data()が指す先頭からsize()バイト分ファイルの中身を読む
		file.read(m_byteCode.data(), m_byteCode.size());

		return true;
	}


	D3D12_SHADER_BYTECODE Shader::GetByteCode() const
	{
		D3D12_SHADER_BYTECODE shaderByteCode{};
		shaderByteCode.pShaderBytecode = m_byteCode.data();
		shaderByteCode.BytecodeLength  = m_byteCode.size();
		return shaderByteCode;
	}
} // namespace fangEngine::render
