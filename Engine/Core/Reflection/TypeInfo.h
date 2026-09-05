/**
 * @file TypeInfo.h
 * @brief フィールド名での読み書きに答える、型 1 個ぶんの素性の集まり。
 */
#pragma once

#include "Core/Reflection/FieldInfo.h"
#include "Core/Reflection/FieldValue.h"
#include <span>
#include <string_view>


namespace fang
{
	/**
	 * @brief FANG_REFLECT_BEGIN / FANG_REFLECT_END が作る、型 1 個ぶんのフィールド一覧。
	 * @details fields の実体は所有者の static constexpr 配列。TypeInfo 自身も static constexpr で
	 *          持たれるので、GetTypeInfo() が返す参照は起動から終了まで生き続ける。
	 */
	struct TypeInfo
	{
		std::span<const FieldInfo> fields;

		constexpr explicit TypeInfo(std::span<const FieldInfo> fieldsIn)
			: fields(fieldsIn)
		{
		}

		/**
		 * @brief 名前でフィールドを探す。
		 * @return 見つからなければ nullptr。
		 */
		[[nodiscard]] const FieldInfo* FindField(std::string_view fieldName) const;

		/**
		 * @brief フィールドの値を名前で読む。
		 * @param object    読む対象。この TypeInfo を持つ型のインスタンスであること。
		 * @param fieldName フィールド名。
		 * @param outValue  読めたときだけ書く。
		 * @return 名前が無ければ false。
		 */
		[[nodiscard]] bool TryGetField(const void* object, std::string_view fieldName, FieldValue* outValue) const;

		/**
		 * @brief フィールドの値を名前で書く。
		 * @param object    書く対象。この TypeInfo を持つ型のインスタンスであること。
		 * @param fieldName フィールド名。
		 * @param value     書く値。
		 * @return 名前が無い、または value の型がフィールドの型と違えば false（何も書かない）。
		 *         範囲を持つフィールドは書く前に範囲へ丸める。
		 */
		[[nodiscard]] bool TrySetField(void* object, std::string_view fieldName, const FieldValue& value) const;
	};
} // namespace fang
