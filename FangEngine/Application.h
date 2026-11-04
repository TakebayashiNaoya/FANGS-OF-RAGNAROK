/**
 * @file Application.h
 * @brief アプリケーションクラス
 */
#pragma once
#include <Windows.h>
#include "Render/RenderContext.h"


namespace fangEngine
{
	/**
	 * @brief ウィンドウと各サブシステムを持ち、フレームループを回すクラス
	 */
	class Application
	{
	public:
		/**
		 * @brief ウィンドウと各サブシステムを作る
		 * @return 全部作成できた場合はtrue、1つでも失敗した場合はfalse
		 */
		bool Initialize();

		/**
		 * @brief ウィンドウが閉じられるまでフレームループを回す
		 * @return 閉じられて抜けた場合はtrue、描画に失敗した場合はfalse
		 */
		bool Run();


	private:
		/**
		 * @brief ウィンドウクラスを登録し、ウィンドウを作って表示する
		 * @return 作ったウィンドウ、失敗したらnullptr
		 */
		HWND CreateMainWindow();


	private:
		render::RenderContext m_renderContext;
	};
} // namespace fangEngine
