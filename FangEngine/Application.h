/**
 * @file Application.h
 * @brief アプリケーションクラス
 */
#pragma once
#include <Windows.h>
#include "Render/RenderContext.h"
#include "Render/DebugOverlay.h"
#include "Core/Memory/Memory.h"
#include "Render/ModelPass.h"


namespace fangEngine
{
	/**
	 * @brief ウィンドウと各サブシステムを持ち、フレームループを回すクラス
	 */
	class Application
	{
	public:
		/**
		 * @brief GPUの処理が終わるのを待ってから破棄する
		 */
		~Application();

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
		render::ModelPass     m_modelPass;
		render::Model         m_wolfModel;
		render::DebugOverlay  m_debugOverlay; // RenderContextのDeviceを使うため、先に壊れるように下に置く

		core::Memory m_memory;
		void*        m_firstBlock  = nullptr; // 確認用に確保した64バイトのブロック
		void*        m_secondBlock = nullptr; // 確認用に確保した32バイトのブロック

		core::Matrix m_viewProjection{}; // カメラの見え方を決める行列。カメラの段まで仮に持つ

		float m_modelAngle = 0.0f; // モデルの回転角度(ラジアン)
	};
} // namespace fangEngine
