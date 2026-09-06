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
		Count,
	};

	/**
	 * @brief 次に Begin するウィンドウを、左端 1 列の決まった席へ畳んだ状態で置く。
	 * @details ImGuiCond_FirstUseEver で置くので、効くのは初回だけ。保存済みの imgui.ini やマウスで
	 *          動かした後の位置には負ける（このエンジンは imgui.ini を読まないので、実際に負けるのは
	 *          マウス操作の分だけ）。Begin の直前で呼ぶこと。
	 */
	void ApplyPanelPlacement(EnPanelSlot slot);
} // namespace fang::editor
