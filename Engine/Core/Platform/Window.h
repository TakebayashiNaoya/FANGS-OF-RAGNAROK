/**
 * @file Window.h
 * @brief OS のウィンドウ。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <cstdint>


namespace fang
{
	/** @brief ウィンドウの生成条件。 */
	struct WindowDesc
	{
		const wchar_t* title  = L"FANGS OF RAGNAROK"; /**< タイトルバーに出す文字列。 */
		uint32_t       width  = 1280;                 /**< クライアント領域の幅（ピクセル）。枠は含まない。 */
		uint32_t       height = 720;                  /**< クライアント領域の高さ（ピクセル）。 */
	};

	/**
	 * @brief OS のウィンドウ。
	 * @details Windows は HWND、Xbox（UWP）は CoreWindow。どちらも GetNativeHandle() で RHI に渡す。
	 * @threading メインスレッドのみ。
	 */
	class Window
	{
	public:
		FANG_NON_COPYABLE(Window);

		Window() = default;
		~Window();

		/**
		 * @brief ウィンドウを作って表示する。
		 * @param desc 生成条件。width / height はクライアント領域の大きさ（ピクセル）。枠は含まない。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool Initialize(const WindowDesc& desc);

		/** @brief ウィンドウを閉じて解放する。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief たまったメッセージを処理する。
		 * @return 閉じられていなければ true。
		 */
		[[nodiscard]] bool PumpMessages();

		/**
		 * @brief 前回の問い合わせ以降にサイズが変わっていたら true を返し、変更を消費する。
		 * @details 呼ぶ側はこれが true のときだけスワップチェーンを作り直せばよい。
		 */
		[[nodiscard]] bool ConsumeSizeChange();

		/** @brief RHI にスワップチェーンを作らせるための OS のハンドル（Windows なら HWND）。 */
		[[nodiscard]] FANG_FORCEINLINE void* GetNativeHandle() const { return m_nativeHandle; }

		/** @brief クライアント領域の幅（ピクセル）。枠は含まない。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetWidth() const { return m_width; }

		/** @brief クライアント領域の高さ（ピクセル）。 */
		[[nodiscard]] FANG_FORCEINLINE uint32_t GetHeight() const { return m_height; }

		/** @brief サイズが変わったことを記録する。プラットフォーム実装から呼ぶ用で、ゲームからは呼ばない。 */
		void OnResized(uint32_t width, uint32_t height);


	private:
		void*    m_nativeHandle     = nullptr; /**< Windows なら HWND。 */
		uint32_t m_width            = 0;       /**< クライアント領域の幅。枠は含まない。 */
		uint32_t m_height           = 0;       /**< クライアント領域の高さ。 */
		bool     m_isCloseRequested = false;   /**< WM_QUIT を受け取ったか。 */
		bool     m_isSizeChanged    = false;   /**< ConsumeSizeChange() で読むと下がる。 */
	};
} // namespace fang
