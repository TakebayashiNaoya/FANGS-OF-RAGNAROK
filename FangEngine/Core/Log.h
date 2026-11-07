/**
 * @file Log.h
 * @brief ログ出力の関数をまとめたファイル
 */
#pragma once
#include <Windows.h>


namespace fangEngine::core
{
	/**
	 * @brief 出力とウィンドウにエラーの文を出す
	 * @param message 出力するエラーの文字列
	 */
	void LogError(const wchar_t* message);

	/**
	 * @brief resultが失敗だった場合に、出力とウィンドウにエラーの文を出す
	 * @param result HRESULTの結果
	 * @param message 出力するエラーの文字列
	 * @return resultが失敗だった場合はtrue、成功だった場合はfalse
	 */
	bool LogIfFailed(HRESULT result, const wchar_t* message);
} // namespace fangEngine::core
