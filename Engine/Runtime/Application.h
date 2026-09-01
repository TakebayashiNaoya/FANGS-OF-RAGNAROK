/**
 * @file Application.h
 * @brief アプリケーションの起動口とフレームループ。
 */
#pragma once

#include "Runtime/EngineContext.h"


namespace fang
{
	class Window;
} // namespace fang

namespace fang::rhi
{
	class CommandList;
	class GraphicsDevice;
} // namespace fang::rhi

namespace fang
{
	/**
	 * @brief フレームループから上の層へ戻る口。
	 * @details Runtime は Editor も Game も知らないので、上の層はこれを継承して合流する。
	 *          Unity の MonoBehaviour、UE の AActor::Tick と同じ役割。
	 * @threading メインスレッドのみ。
	 */
	class IApplication
	{
	public:
		virtual ~IApplication() = default;

		/**
		 * @brief デバイスができた直後に 1 回。false を返すと起動を中止する。
		 * @param context 中身の寿命はフレームループより長い。参照を持ち続けてよい。
		 */
		[[nodiscard]] virtual bool OnInitialize(
			const EngineContext& context,
			rhi::GraphicsDevice& device,
			const Window&        window
		) = 0;

		/** @brief 毎フレーム、描画を始める前に呼ばれる。ゲームの状態を進める。 */
		virtual void OnUpdate(const Window& window, float deltaTimeSeconds) = 0;

		/** @brief 毎フレーム、バックバッファのクリア後に呼ばれる。描画コマンドを積む。 */
		virtual void OnRender(rhi::GraphicsDevice& device, rhi::CommandList& commandList) = 0;

		/** @brief 終了時に 1 回。 */
		virtual void OnShutdown(rhi::GraphicsDevice& device) = 0;
	};

	/**
	 * @brief アプリケーションを起動し、終了コードを返す。
	 * @return プロセスの終了コード。
	 * @threading メインスレッドのみ。
	 */
	int RunApplication(IApplication& application);
} // namespace fang
