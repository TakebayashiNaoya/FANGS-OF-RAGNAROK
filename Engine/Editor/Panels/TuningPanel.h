/**
 * @file TuningPanel.h
 * @brief 登録された調整値を、入れ子も含めてつまみへ写すパネル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Reflection/TuningRow.h"
#include <array>


namespace fang::editor
{
	/**
	 * @brief 「調整値」ウィンドウ 1 枚。登録された実体のつまみを段付きで並べる。
	 * @details 行を 1 対 1 でつまみへ写すだけで、型の宣言を 1 つも知らない（void* と const TypeInfo& だけ）。
	 *          触られた値は登録簿の控え帳へ積み、実体へ入れるのはフレームループ。
	 * @threading 組み立てはメインスレッドのみ。
	 */
	class TuningPanel
	{
	public:
		FANG_NON_COPYABLE(TuningPanel);

		TuningPanel()  = default;
		~TuningPanel() = default;

		/** @return 失敗しない。他のパネルと戻り値の形をそろえるため bool を返す。 */
		[[nodiscard]] bool Initialize();
		void               Shutdown();

		/** @brief このフレームの内容を組み立てる。畳んでいるフレームは行を 1 本も作らない。 */
		void BuildFrame();


	private:
		/** @brief つまみ 1 個。動かされたら控え帳へ積む。 */
		void BuildRowWidget(const TuningRow& row) const;


	private:
		/** @brief このフレームの行。開いているフレームだけ組み立て直す。 */
		std::array<TuningRow, MAX_TUNING_ROW_COUNT> m_rows;

		TuningRowBuildResult m_buildResult;
	};
} // namespace fang::editor
