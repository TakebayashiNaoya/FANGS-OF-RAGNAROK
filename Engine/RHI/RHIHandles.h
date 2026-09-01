/**
 * @file RHIHandles.h
 * @brief GPU リソースを指す世代付きハンドル。
 */
#pragma once

#include "Core/CoreMacros.h"
#include <cstdint>


namespace fang::rhi
{
	/**
	 * @brief 世代付きインデックス。
	 * @details ポインタをフレームをまたいで持たないための参照。
	 *          解放済みのスロットを再利用しても、世代が違えば古いハンドルは無効と分かる。
	 */
	template <typename Tag> struct Handle
	{
		static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

		uint32_t index      = INVALID_INDEX; /**< 配列上の位置。 */
		uint32_t generation = 0;             /**< スロットが再利用された回数。古いハンドルを見分けるためのもの。 */

		[[nodiscard]] FANG_FORCEINLINE bool IsValid() const { return index != INVALID_INDEX; }
	};

	struct PipelineTag
	{
	};

	struct BufferTag
	{
	};

	struct TextureTag
	{
	};

	using PipelineHandle = Handle<PipelineTag>;
	using BufferHandle   = Handle<BufferTag>;
	using TextureHandle  = Handle<TextureTag>;
} // namespace fang::rhi
