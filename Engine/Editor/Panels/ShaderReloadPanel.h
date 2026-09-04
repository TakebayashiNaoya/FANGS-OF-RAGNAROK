/**
 * @file ShaderReloadPanel.h
 * @brief シェーダーの作り直しの結果（見張りの有無・成否の回数・直近のメッセージ）を出すパネル。
 */
#pragma once

#include "Core/CoreMacros.h"


namespace fang::rhi
{
	struct ShaderReloadStatus;
} // namespace fang::rhi


namespace fang::editor
{
	/**
	 * @brief 「シェーダーホットリロード」ウィンドウ 1 枚。
	 * @details .hlsl を保存したときに、効いたのか・どこで失敗したのかを画の横で読めるようにする。
	 *          自分では何も測らず、RHI が控えた結果をそのまま出す。
	 * @threading 組み立てはメインスレッドのみ。
	 */
	class ShaderReloadPanel
	{
	public:
		FANG_NON_COPYABLE(ShaderReloadPanel);

		ShaderReloadPanel()  = default;
		~ShaderReloadPanel() = default;

		/**
		 * @brief 結果の出どころを控える。
		 * @param status GraphicsDevice が持っている結果。Shutdown まで生きていること。
		 *               ホットリロードを持たない構成では nullptr が来る。
		 * @return 失敗しない。他のパネルと戻り値の形をそろえるため bool を返す。
		 */
		[[nodiscard]] bool Initialize(const rhi::ShaderReloadStatus* status);

		/** @brief 控えた出どころを手放す。二重に呼んでも安全。 */
		void Shutdown();

		/** @brief このフレームの内容を組み立てる。ImGui::NewFrame の後に呼ぶ。 */
		void BuildFrame() const;


	private:
		/** @brief 結果の出どころ。GraphicsDevice が持っているものを借りるだけ。 */
		const rhi::ShaderReloadStatus* m_status = nullptr;
	};
} // namespace fang::editor
