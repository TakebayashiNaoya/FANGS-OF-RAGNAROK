/**
 * @file BudgetPanel.h
 * @brief Xbox の予算（メモリ 5GB と 60fps）に対する現在値を出し、CPU の換算と制限を切り替えるパネル。
 */
#pragma once

#include "Core/CoreMacros.h"


namespace fang
{
	class PlatformBudget;
	struct EngineContext;
} // namespace fang


namespace fang::editor
{
	/**
	 * @brief 「Xbox 予算」ウィンドウ 1 枚。
	 * @details PC で作っていると実機の狭さが見えないので、Xbox の割り当てに対する今の値を常に横に出す。
	 *          自分では測らず、PlatformBudget が控えた値を出して、倍率と制限の入切だけを書き戻す。
	 * @threading 組み立てはメインスレッドのみ。
	 */
	class BudgetPanel
	{
	public:
		FANG_NON_COPYABLE(BudgetPanel);

		BudgetPanel()  = default;
		~BudgetPanel() = default;

		/**
		 * @brief 予算の出どころを控える。
		 * @param context 中身は呼び出し側が生かし続けるので、予算の参照だけ控える。
		 * @return 失敗しない。他のパネルと戻り値の形をそろえるため bool を返す。
		 */
		[[nodiscard]] bool Initialize(const EngineContext& context);

		/** @brief 控えた出どころを手放す。二重に呼んでも安全。 */
		void Shutdown();

		/** @brief このフレームの内容を組み立てる。ImGui::NewFrame の後に呼ぶ。 */
		void BuildFrame();


	private:
		/** @brief メモリの使用量と最高水位を出す。 */
		void BuildMemorySection() const;

		/** @brief フレーム時間の Xbox 換算と、倍率・制限の操作を出す。 */
		void BuildCpuSection();


	private:
		/** @brief 予算の出どころ。RunApplication が持っているものを借りるだけ。 */
		PlatformBudget* m_budget = nullptr;
	};
} // namespace fang::editor
