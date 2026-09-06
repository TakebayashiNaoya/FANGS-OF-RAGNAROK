/**
 * @file Reflection.h
 * @brief FANG_REFLECT_BEGIN / FANG_FIELD / FANG_REFLECT_END と、Reflection モジュールの入口。
 * @details 使い方は 03 コーディング規約 14。フィールドを並べた POD に付けると、GetTypeInfo() が
 *          その場で（後ろに書いたメンバも含めて）フィールド一覧を組み立てる。
 *
 * @code
 * struct WolfMovementParameter
 * {
 *     FANG_REFLECT_BEGIN(WolfMovementParameter)
 *         FANG_FIELD(moveSpeedCentimetersPerSecond, "移動速度", Range(0.0f, 2000.0f))
 *         FANG_FIELD(turnSpeedRadiansPerSecond,     "旋回速度", Range(0.0f, 32.0f))
 *     FANG_REFLECT_END()
 *
 *     float moveSpeedCentimetersPerSecond = 400.0f;
 *     float turnSpeedRadiansPerSecond     = 8.0f;
 * };
 * @endcode
 */
#pragma once

#include "Core/Reflection/FieldInfo.h"
#include "Core/Reflection/FieldValue.h"
#include "Core/Reflection/TypeInfo.h"
#include <cstddef>


/**
 * @brief フィールド一覧の宣言を始める。クラス本体の中、フィールドより前に書く。
 * @details GetTypeInfo() の本体は完全クラス文脈で解析されるので、後ろに書いたメンバへの
 *          offsetof / decltype がここから書ける。
 */
#define FANG_REFLECT_BEGIN(ClassName)                                                                                  \
                                                                                                                       \
public:                                                                                                                \
	using FangReflectSelfType = ClassName;                                                                             \
	[[nodiscard]] static const fang::TypeInfo& GetTypeInfo()                                                           \
	{                                                                                                                  \
		static constexpr fang::FieldInfo fangReflectFields[] = {
/**
 * @brief フィールドを 1 個登録する。
 * @param fieldName   宣言する予定のメンバ変数名。
 * @param displayName インスペクタに出す表示名。日本語。
 * @param ...         省略可。Range(min, max) を渡すと書き込み時に丸められる。
 */
#define FANG_FIELD(fieldName, displayName, ...)                                                                        \
	fang::MakeFieldInfo<decltype(FangReflectSelfType::fieldName)>(                                                     \
		#fieldName,                                                                                                    \
		displayName,                                                                                                   \
		offsetof(FangReflectSelfType, fieldName) __VA_OPT__(, ) __VA_ARGS__                                            \
	),

/** @brief フィールド一覧の宣言を終える。 */
#define FANG_REFLECT_END()                                                                                             \
	}                                                                                                                  \
	;                                                                                                                  \
	static constexpr fang::TypeInfo fangReflectTypeInfo{ fangReflectFields };                                          \
	return fangReflectTypeInfo;                                                                                        \
	}
