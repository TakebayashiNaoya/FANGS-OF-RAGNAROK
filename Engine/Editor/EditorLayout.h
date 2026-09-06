/**
 * @file EditorLayout.h
 * @brief パネルの初期配置（左端 1 列に畳んだ状態）を決める席割り。
 */
#pragma once

#include <cstdint>


namespace fang::editor
{
	/** @brief パネルの席。EditorUI::BuildFrame が組み立てる順と揃えてある。 */
	enum class EnPanelSlot : uint32_t
	{
		EngineInformation,
		JobSystem,
		RenderStatistics,
		Budget,
		Tuning,
		ShaderReload,
		Status, /**< 実行中の値。末席、y = 260。 */
		Count,
	};

	/** @brief 「実行中の値」ウィンドウの名前。行き先がゲームへ戻った周に最前面へ出す先としても使う。 */
	inline constexpr const char* STATUS_WINDOW_NAME = "実行中の値";

	/**
	 * @brief 次に Begin するウィンドウを、左端 1 列の決まった席へ置く。
	 * @param slot                次に Begin するウィンドウの席。
	 * @param isCollapsedAtStart  初回を畳んだ状態にするか。実行中の値だけ false。
	 * @details ImGuiCond_FirstUseEver で置くので、効くのは初回だけ。保存済みの imgui.ini やマウスで
	 *          動かした後の位置には負ける（このエンジンは imgui.ini を読まないので、実際に負けるのは
	 *          マウス操作の分だけ）。Begin の直前で呼ぶこと。
	 */
	void ApplyPanelPlacement(EnPanelSlot slot, bool isCollapsedAtStart = true);
} // namespace fang::editor
