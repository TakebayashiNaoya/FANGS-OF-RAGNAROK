/**
 * @file Shader.h
 * @brief シェーダーを読み込むクラス
 */
#pragma once
#include <d3d12.h>
#include <vector>


namespace fangEngine::render
{
	/**
	 * @brief シェーダーを読み込むクラス
	 * @details ビルドで作った.csoの中身を持ち、実行中にコンパイルしないため.hlslは読まない。
	 */
	class Shader
	{
	public:
		/**
		 * @brief シェーダーを読み込む
		 * @param fileName 読み込むシェーダーのファイル名
		 * @return 読めたらtrue、読めなかったらfalse
		 */
		bool Load(const wchar_t* fileName);

		/**
		 * @brief 読み込んだシェーダーのバイトコードを返す
		 * @return 読み込んだシェーダーのバイトコード
		 */
		D3D12_SHADER_BYTECODE GetByteCode() const;


	private:
		std::vector<char> m_byteCode; // 読んだ.csoの中身
	};
} // namespace fangEngine::render
